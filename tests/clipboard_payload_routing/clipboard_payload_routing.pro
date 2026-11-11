QT += core gui network
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = clipboard_payload_routing
TEMPLATE = app

INCLUDEPATH += \
    $$PWD/../../app

SOURCES += \
    main.cpp \
    ../../app/streaming/clipboardframe.cpp \
    ../../app/streaming/clipboardsync.cpp

macx:SOURCES += ../../clipboard-helper/macos.mm

HEADERS += \
    ../../app/streaming/clipboardblobpolicy.h \
    ../../app/streaming/clipboardframe.h \
    ../../app/streaming/clipboardlogging.h \
    ../../app/streaming/clipboardsync.h

SOURCES += regression.cpp ../../app/streaming/clipboardipc.cpp
HEADERS += ../../app/streaming/clipboardipc.h
RESOURCES += fixtures.qrc
lite-native-http: include(../../clipboard-helper/native-http.pri)
