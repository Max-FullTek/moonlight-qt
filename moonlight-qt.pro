TEMPLATE = subdirs
SUBDIRS = \
    moonlight-common-c \
    app \
    h264bitstream

# Build the dependencies in parallel before the final app
app.depends = moonlight-common-c h264bitstream
!lite-gpui-worker {
    SUBDIRS += qmdnsengine
    app.depends += qmdnsengine
}
!config_SL:!lite-native-clipboard-helper {
    SUBDIRS += clipboard-helper
    app.depends += clipboard-helper
}
win32:!winrt {
    SUBDIRS += AntiHooking
    app.depends += AntiHooking
}

# Support debug and release builds from command line for CI
CONFIG += debug_and_release

# Run our compile tests
load(configure)
qtCompileTest(SL)
qtCompileTest(EGL)
