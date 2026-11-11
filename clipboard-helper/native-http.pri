!win32|!contains(QT_ARCH, x86_64): error("Native clipboard HTTP requires Windows x64")
NATIVE_HTTP_BUILD = $$(LITE_NATIVE_HTTP_BUILD)
isEmpty(NATIVE_HTTP_BUILD): error("Set LITE_NATIVE_HTTP_BUILD to the prepared native HTTP build")
CONFIG(release, debug|release): NATIVE_HTTP_CONFIG = Release
else: NATIVE_HTTP_CONFIG = Debug
NATIVE_HTTP_LIB = $$NATIVE_HTTP_BUILD/host-http/$$NATIVE_HTTP_CONFIG/lite-native-host-http.lib
CURL_LIB = $$NATIVE_HTTP_BUILD/host-http/curl/lib/$$NATIVE_HTTP_CONFIG/libcurl.lib
OPENSSL_LIBS = $$PWD/../libs/windows/lib/x64
DEFINES += LITE_NATIVE_CLIPBOARD_HTTP
SOURCES += $$PWD/../app/streaming/qtclipboardtransfer.cpp
SOURCES += $$PWD/../app/streaming/windowsclipboardlistener.cpp
HEADERS += $$PWD/../app/streaming/qtclipboardtransfer.h
LIBS += $$quote($$NATIVE_HTTP_LIB) $$quote($$CURL_LIB) \
    $$quote($$OPENSSL_LIBS/libssl.lib) $$quote($$OPENSSL_LIBS/libcrypto.lib) \
    ws2_32.lib wldap32.lib bcrypt.lib crypt32.lib iphlpapi.lib user32.lib
PRE_TARGETDEPS += $$quote($$NATIVE_HTTP_LIB) $$quote($$CURL_LIB)
