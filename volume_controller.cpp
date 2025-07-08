#include "VolumeController.hpp"
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QStringList>

// USB volume knob device detection
static QString findInputDevice() {
    // Default values (overridden by config file if present)
    unsigned short VENDOR_ID = 0x68e;  // Default Vendor ID
    unsigned short PRODUCT_ID = 0x566; // Default Product ID

    // Read config file
    QFile configFile("/etc/default/usbvol");
    if (configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith("#")) continue; // Skip comments and empty lines
            QStringList parts = line.split("=");
            if (parts.size() == 2) {
                QString key = parts[0].trimmed();
                QString value = parts[1].trimmed();
                if (key == "VENDOR_ID") VENDOR_ID = value.toUInt(nullptr, 16);
                else if (key == "PRODUCT_ID") PRODUCT_ID = value.toUInt(nullptr, 16);
            }
        }
        configFile.close();
    } else {
        qDebug() << "Config file /etc/default/usbvol not found, using defaults: Vendor ID" << QString::number(VENDOR_ID, 16) << ", Product ID" << QString::number(PRODUCT_ID, 16);
    }

    QDir dir("/dev/input");
    QStringList devices = dir.entryList(QStringList() << "event*", QDir::System | QDir::Files, QDir::Name);

    for (const QString &device : devices) {
        QString path = QString("/dev/input/%1").arg(device);
        int fd = open(path.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            qDebug() << "Cannot open" << path << ":" << strerror(errno);
            continue;
        }

        struct libevdev *dev = nullptr;
        int rc = libevdev_new_from_fd(fd, &dev);
        if (rc < 0) {
            qDebug() << "Failed to init libevdev for" << path << ":" << strerror(-rc);
            close(fd);
            continue;
        }

        // Check Vendor ID and Product ID
        if (libevdev_get_id_vendor(dev) == VENDOR_ID && libevdev_get_id_product(dev) == PRODUCT_ID) {
            qDebug() << "Device found - Name:" << libevdev_get_name(dev) << ", Vendor ID:" << QString::number(VENDOR_ID, 16) << ", Product ID:" << QString::number(PRODUCT_ID, 16) << "at" << path;
            if (libevdev_has_event_code(dev, EV_KEY, KEY_VOLUMEUP) &&
                libevdev_has_event_code(dev, EV_KEY, KEY_VOLUMEDOWN) &&
                libevdev_has_event_code(dev, EV_KEY, KEY_MUTE)) {
                qDebug() << "Verified volume knob device:" << libevdev_get_name(dev) << "at" << path;
                libevdev_free(dev);
                close(fd);
                return path;
            } else {
                qDebug() << "Device at" << path << "does not support required keys, skipping";
            }
        }

        libevdev_free(dev);
        close(fd);
    }

    qDebug() << "No suitable input device found with Vendor ID" << QString::number(VENDOR_ID, 16) << "and Product ID" << QString::number(PRODUCT_ID, 16);
    return QString();
}

VolumeController::VolumeController(QObject *parent) : QObject(parent), ci(new clControlInterface(this)), fd(-1), dev(nullptr), currentVolume(-144.0), preMuteVolume(-144.0), minVolume(-144.0), maxVolume(0.0), stepSize(1.0), isMuted(false) {
    // Read config file for volume and step size settings
    QFile configFile("/etc/default/usbvol");
    if (configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&configFile);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith("#")) continue; // Skip comments and empty lines
            QStringList parts = line.split("=");
            if (parts.size() == 2) {
                QString key = parts[0].trimmed();
                QString value = parts[1].trimmed();
                if (key == "MIN_VOLUME") minVolume = value.toDouble();
                else if (key == "DEFAULT_VOLUME") currentVolume = value.toDouble();
                else if (key == "MUTE_VOLUME") preMuteVolume = value.toDouble(); // Used as mute volume reference
                else if (key == "STEP_SIZE") stepSize = value.toDouble();
            }
        }
        configFile.close();
    } else {
        qDebug() << "Config file /etc/default/usbvol not found, using defaults: Min Volume" << minVolume << ", Default Volume" << currentVolume << ", Mute Volume" << preMuteVolume << ", Step Size" << stepSize;
    }

    ci->connectToHost("localhost", 4321);
    connect(ci, &clControlInterface::connected, this, &VolumeController::onConnected);
    connect(ci, &clControlInterface::error, this, &VolumeController::onError);
    connect(ci, &clControlInterface::volumeRangeResponse, this, &VolumeController::onVolumeRangeResponse);
    connect(ci, &clControlInterface::stateResponse, this, &VolumeController::onStateResponse);
    initInputDevice();
}

VolumeController::~VolumeController() {
    if (dev) libevdev_free(dev);
    if (fd >= 0) close(fd);
}

void VolumeController::initInputDevice() {
    QString devicePath = findInputDevice();
    if (devicePath.isEmpty()) {
        qDebug() << "No volume knob device found, exiting";
        QCoreApplication::quit();
        return;
    }

    fd = open(devicePath.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        qDebug() << "Failed to open input device" << devicePath << ":" << strerror(errno);
        QCoreApplication::quit();
        return;
    }

    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        qDebug() << "Failed to init libevdev for" << devicePath << ":" << strerror(-rc);
        close(fd);
        QCoreApplication::quit();
        return;
    }

    qDebug() << "Input device:" << libevdev_get_name(dev) << "at" << devicePath;
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &VolumeController::readInputEvents);
    timer->start(10);
}

void VolumeController::readInputEvents() {
    struct input_event ev;
    int rc;

    while ((rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev)) >= 0) {
        if (rc == LIBEVDEV_READ_STATUS_SUCCESS && ev.type == EV_KEY && ev.value == 1) {
            switch (ev.code) {
                case KEY_VOLUMEUP:
                    if (!isMuted) {
                        double newVolume = qBound(minVolume, currentVolume + stepSize, maxVolume);
                        if (newVolume != currentVolume) {
                            currentVolume = newVolume;
                            ci->volume(currentVolume);
                            qDebug() << "Volume up to:" << currentVolume << "dB (step size:" << stepSize << ")";
                        }
                    }
                    break;
                case KEY_VOLUMEDOWN:
                    if (!isMuted) {
                        double newVolume = qBound(minVolume, currentVolume - stepSize, maxVolume);
                        if (newVolume != currentVolume) {
                            currentVolume = newVolume;
                            ci->volume(currentVolume);
                            qDebug() << "Volume down to:" << currentVolume << "dB (step size:" << stepSize << ")";
                        }
                    }
                    break;
                case KEY_MUTE:
                    if (!isMuted) {
                        preMuteVolume = currentVolume;
                        isMuted = true;
                        ci->volume(minVolume); // Use MIN_VOLUME for mute
                        qDebug() << "Mute: On, set to" << minVolume << "dB";
                    } else {
                        isMuted = false;
                        ci->volume(preMuteVolume); // Restore previous volume
                        qDebug() << "Mute: Off, restored volume to:" << preMuteVolume << "dB";
                    }
                    break;
                default:
                    break;
            }
        }
    }
    if (rc != -EAGAIN && rc < 0) {
        qDebug() << "Error reading input events:" << strerror(-rc);
    }
}

void VolumeController::onConnected() {
    qDebug() << "Connected to HQPlayer Embedded";
    ci->volumeRange();
    ci->state();
}

void VolumeController::onError(QString error) {
    qDebug() << "Error:" << error;
    QTimer::singleShot(5000, this, [this]() {
        qDebug() << "Retrying connection to HQPlayer Embedded...";
        ci->connectToHost("localhost", 4321);
    });
}

void VolumeController::onVolumeRangeResponse(double min, double max, bool enabled, bool adaptive) {
    minVolume = min;
    maxVolume = max;
    qDebug() << "Volume range:" << min << "to" << max << "(enabled:" << enabled << ", adaptive:" << adaptive << ")";
}

void VolumeController::onStateResponse(int state, int mode, int filter, int filter1x, int filterNx, int shaper, int rate,
                                      double volume, unsigned active_mode, unsigned active_rate,
                                      bool invert, bool convolution, int repeat, bool random, bool adaptive, bool filter20k,
                                      QString matrixProfile) {
    Q_UNUSED(state); Q_UNUSED(mode); Q_UNUSED(filter); Q_UNUSED(filter1x); Q_UNUSED(filterNx);
    Q_UNUSED(shaper); Q_UNUSED(rate); Q_UNUSED(active_mode); Q_UNUSED(active_rate);
    Q_UNUSED(invert); Q_UNUSED(convolution); Q_UNUSED(repeat); Q_UNUSED(random);
    Q_UNUSED(adaptive); Q_UNUSED(filter20k); Q_UNUSED(matrixProfile);
    currentVolume = volume;
    preMuteVolume = volume;
    qDebug() << "Current volume from HQPlayer:" << currentVolume << "dB";
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    VolumeController controller;
    return app.exec();
}
