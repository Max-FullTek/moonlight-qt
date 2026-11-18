QT += core gui testlib
CONFIG += console c++17
CONFIG -= app_bundle
INCLUDEPATH += ../../app
SOURCES += main.cpp ../../app/streaming/video/overlaymenupanel.cpp
HEADERS += \
    ../../app/streaming/video/overlayrasterwindow.h \
    ../../app/streaming/video/overlaymenupanel.h
RESOURCES += fonts.qrc

win32:lite-gpui-worker {
    DEFINES += LITE_GPUI_WORKER
    RESOURCES += ../../app/lite-overlay-icons.qrc
} else {
    QT += svg
    RESOURCES += ../../app/resources.qrc
}
