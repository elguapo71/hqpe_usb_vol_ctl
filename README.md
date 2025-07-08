# hqpe_usb_vol_ctl
Please visit signalyst.com and download the HQPlayer SDK first.\
https://signalyst.com/downloads/
Extract it to your home directory. Folder name should be something like 'hqp-control-*'. Enter the folder and change the 'control.pro' file name to 'control.pro.bak' since we will create a new project for it.
Due to Qt5 compatibility, please edit the 'ControlInterface.cpp', find this block:

void clMeterInterface::onReadyRead() {
    // ...
    QByteArray meterHead(readBuffer.first(sizeHead));
    readBuffer.remove(0, sizeHead);
    QByteArray meterData(readBuffer.first(sizeData));
    // ...
}

Replace 'first' to 'left' like this and save:

void clMeterInterface::onReadyRead() {
    // ... (previous code unchanged)
    QByteArray meterHead(readBuffer.left(sizeHead));
    readBuffer.remove(0, sizeHead);
    QByteArray meterData(readBuffer.left(sizeData));
    // ... (rest of the function unchanged)
}

Then put this project's 'usbvol.pro', 'VolumeController.hpp' and 'volume_controller.cpp' into the 'hqp-control-*' folder.

make clean
qmake
make

Application 'usbvol' will be built.

Create a configuration file:

sudo nano /etc/default/usbvol

put these line to usbvol file and save:

VENDOR_ID=0x68e
PRODUCT_ID=0x566
MIN_VOLUME=-60.0
DEFAULT_VOLUME=-30.0
MUTE_VOLUME=-60.0
STEP_SIZE=1.0

Run usbvol then done.

You can also create a systemd startup file:

sudo nano /etc/systemd/system/usbvol.service

Put these into the file:

[Unit]
Description=USB Volume Knob Controller for HQPlayer Embedded
After=network-online.target hqplayerd.service
Requires=hqplayerd.service

[Service]
ExecStart=/directory/to/usbvol
WorkingDirectory=/directory/to/usbvol
Restart=always

[Install]
WantedBy=multi-user.target

And then you can use systemctl start / stop / restart the usbvol app.
