QT += core network xml
QT -= gui
CONFIG += c++11
TARGET = usbvol
SOURCES += volume_controller.cpp ControlInterface.cpp
HEADERS += VolumeController.hpp ControlInterface.hpp
LIBS += -levdev -lbotan-2
INCLUDEPATH += /usr/include/libevdev-1.0 /usr/include/botan-2