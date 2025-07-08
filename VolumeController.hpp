#ifndef VOLUMECONTROLLER_HPP
#define VOLUMECONTROLLER_HPP

#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <libevdev-1.0/libevdev/libevdev.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "ControlInterface.hpp"

class VolumeController : public QObject {
    Q_OBJECT

public:
    VolumeController(QObject *parent = nullptr);
    ~VolumeController();

private:
    clControlInterface *ci;
    int fd;
    struct libevdev *dev;
    double currentVolume;
    double preMuteVolume;
    double minVolume;
    double maxVolume;
    double stepSize;
    bool isMuted;

    void initInputDevice();
    void readInputEvents();

private slots:
    void onConnected();
    void onError(QString error);
    void onVolumeRangeResponse(double min, double max, bool enabled, bool adaptive);
    void onStateResponse(int state, int mode, int filter, int filter1x, int filterNx, int shaper, int rate,
                         double volume, unsigned active_mode, unsigned active_rate,
                         bool invert, bool convolution, int repeat, bool random, bool adaptive, bool filter20k,
                         QString matrixProfile);
};

#endif // VOLUMECONTROLLER_HPP