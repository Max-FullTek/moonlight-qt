QT += core gui
CONFIG += console c++17
CONFIG -= app_bundle

INCLUDEPATH += ../../app

SOURCES += \
    main.cpp \
    ../../app/settings/devicelocalsettings.cpp \
    ../../app/streaming/video/overlaybuttonposition.cpp

HEADERS += \
    ../../app/settings/devicelocalsettings.h \
    ../../app/streaming/video/overlaybuttonposition.h \
    ../../app/streaming/video/overlayplacementaxis.h

win32:lite-gpui-worker {
    DEFINES += LITE_GPUI_WORKER
    SOURCES += ../../app/backend/legacyworkerprofile.cpp \
               ../../app/backend/windowsprofilefile.cpp
}
