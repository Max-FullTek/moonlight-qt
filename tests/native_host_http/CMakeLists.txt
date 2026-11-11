cmake_minimum_required(VERSION 3.24)
project(native_host_http_capability LANGUAGES C CXX)
add_executable(native_session_rpc session_rpc.cpp)
target_compile_features(native_session_rpc PRIVATE cxx_std_17)
target_include_directories(native_session_rpc PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../app")
target_compile_options(native_session_rpc PRIVATE /utf-8 /W4 /WX /UNDEBUG)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This probe requires Windows x64 MSVC")
endif()
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../windows-lite/host-http" host-http)
add_library(native-pairing STATIC "${CMAKE_CURRENT_LIST_DIR}/../../app/backend/nativepairing.cpp")
target_compile_features(native-pairing PUBLIC cxx_std_17)
target_compile_options(native-pairing PRIVATE /utf-8 /W3 /WX /guard:cf /guard:ehcont)
target_include_directories(native-pairing PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../../app" "${CMAKE_CURRENT_LIST_DIR}/../../libs/windows/include/x64")
target_link_libraries(native-pairing PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../../libs/windows/lib/x64/libcrypto.lib")
add_executable(native_pairing_peer "${CMAKE_CURRENT_LIST_DIR}/../native_pairing/main.cpp")
target_compile_options(native_pairing_peer PRIVATE /utf-8 /W3 /WX)
target_link_libraries(native_pairing_peer PRIVATE native-pairing)
add_executable(native_host_http main.cpp)
target_compile_features(native_host_http PRIVATE cxx_std_17)
target_compile_definitions(native_host_http PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(native_host_http PRIVATE /utf-8 /W3 /WX)
target_link_libraries(native_host_http PRIVATE lite-native-host-http)

# Optional legacy-owner verification; the native target above never requires Qt.
option(VERIFY_QT_HTTP_OWNER "Verify the transitional Qt event-loop owner" OFF)
if(VERIFY_QT_HTTP_OWNER)
    find_package(Qt6 REQUIRED COMPONENTS Core Network Gui)
    set(BUILD_SHARED_LIBS OFF)
    set(ENET_NO_INSTALL ON)
    set(USE_MBEDTLS OFF)
    # Same pinned dependency as host-http; do not discover a system OpenSSL.
    set(OPENSSL_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../libs/windows/include/x64")
    set(OPENSSL_CRYPTO_LIBRARY "${CMAKE_CURRENT_LIST_DIR}/../../libs/windows/lib/x64/libcrypto.lib")
    set(LIB_EAY_DEBUG "${OPENSSL_CRYPTO_LIBRARY}")
    set(LIB_EAY_RELEASE "${OPENSSL_CRYPTO_LIBRARY}")
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../moonlight-common-c/moonlight-common-c" common-c)
    get_target_property(common_definitions moonlight-common-c COMPILE_DEFINITIONS)
    list(REMOVE_ITEM common_definitions NDEBUG LC_DEBUG)
    set_property(TARGET moonlight-common-c PROPERTY COMPILE_DEFINITIONS "${common_definitions}")
    target_compile_definitions(moonlight-common-c PRIVATE HAS_QOS_FLOWID=1 HAS_PQOS_FLOWID=1
        $<$<CONFIG:Debug>:LC_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
    target_compile_options(moonlight-common-c PRIVATE /utf-8)
    add_executable(native_host_http_qt_owner main.cpp "${CMAKE_CURRENT_LIST_DIR}/../../app/backend/qtnativehttptransport.cpp")
    target_compile_features(native_host_http_qt_owner PRIVATE cxx_std_17)
    target_compile_definitions(native_host_http_qt_owner PRIVATE NATIVE_HTTP_QT_OWNER _CRT_SECURE_NO_WARNINGS)
    target_compile_options(native_host_http_qt_owner PRIVATE /utf-8 /W3 /WX)
    target_link_libraries(native_host_http_qt_owner PRIVATE lite-native-host-http Qt6::Core)
    add_executable(nvhttp_native nvhttp.cpp
        "${CMAKE_CURRENT_LIST_DIR}/../../app/backend/nvhttp.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../app/backend/nvhttp.h"
        "${CMAKE_CURRENT_LIST_DIR}/../../app/backend/nvcomputer.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../app/backend/windowsreachability.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../app/backend/nvapp.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../app/backend/nvpairingmanager.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../app/settings/compatfetcher.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../app/backend/nvaddress.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../app/backend/identitymanager.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../app/backend/nativeidentity.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../app/backend/qtnativehttptransport.cpp")
    set_target_properties(nvhttp_native PROPERTIES AUTOMOC ON)
    target_include_directories(nvhttp_native PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../moonlight-common-c/moonlight-common-c/src")
    target_compile_features(nvhttp_native PRIVATE cxx_std_17)
    target_compile_definitions(nvhttp_native PRIVATE LITE_NATIVE_HTTP _CRT_SECURE_NO_WARNINGS)
    target_compile_options(nvhttp_native PRIVATE /utf-8 /W3)
    target_link_libraries(nvhttp_native PRIVATE native-pairing lite-native-host-http moonlight-common-c Qt6::Core Qt6::Network Qt6::Gui)
    get_target_property(nvhttp_sources nvhttp_native SOURCES)
    add_executable(nvhttp_legacy ${nvhttp_sources})
    set_target_properties(nvhttp_legacy PROPERTIES AUTOMOC ON)
    target_include_directories(nvhttp_legacy PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../moonlight-common-c/moonlight-common-c/src")
    target_compile_features(nvhttp_legacy PRIVATE cxx_std_17)
    target_compile_definitions(nvhttp_legacy PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(nvhttp_legacy PRIVATE /utf-8 /W3)
    target_link_libraries(nvhttp_legacy PRIVATE native-pairing lite-native-host-http moonlight-common-c Qt6::Core Qt6::Network Qt6::Gui)
    target_link_libraries(nvhttp_native PRIVATE iphlpapi)
    target_link_libraries(nvhttp_legacy PRIVATE iphlpapi)
endif()
