cmake_minimum_required(VERSION 3.24)
project(moonlight-lite-native LANGUAGES C CXX)

# This is the native-library stage, not the Qt-free streaming worker yet.
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    message(FATAL_ERROR "Windows Lite currently requires the MSVC Visual Studio x64 generator")
endif()
enable_language(RC)

set(LITE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
get_filename_component(LITE_SOURCE_ROOT "${LITE_SOURCE_ROOT}" ABSOLUTE)
set(LITE_DEPS_ROOT "${LITE_SOURCE_ROOT}/libs/windows" CACHE PATH
    "Existing Windows dependency archive unpacked by prepare-portable-deps.ps1")
set(LITE_DEPS_LIB "${LITE_DEPS_ROOT}/lib/x64")

foreach(required IN ITEMS include/x64/openssl/ssl.h include/detours.h
                          lib/x64/libcrypto.lib lib/x64/libcrypto-3-x64.dll lib/x64/detours.lib
                          include/x64/SDL2/SDL.h lib/x64/SDL2.lib lib/x64/SDL2.dll lib/x64/SDL3.dll
                          include/opus_multistream.h lib/x64/opus.lib lib/x64/opus.dll)
    if(NOT EXISTS "${LITE_DEPS_ROOT}/${required}")
        message(FATAL_ERROR "Missing ${LITE_DEPS_ROOT}/${required}; prepare the pinned Windows dependencies first")
    endif()
endforeach()

# Do not discover a different OpenSSL installation from PATH or the registry.
set(OPENSSL_INCLUDE_DIR "${LITE_DEPS_ROOT}/include/x64" CACHE PATH "" FORCE)
set(OPENSSL_CRYPTO_LIBRARY "${LITE_DEPS_LIB}/libcrypto.lib" CACHE FILEPATH "" FORCE)
# FindOpenSSL's MSVC branch selects these before assigning its public result.
# The archive supplies a DLL import library shared by Debug/Release consumers.
set(LIB_EAY_DEBUG "${LITE_DEPS_LIB}/libcrypto.lib" CACHE FILEPATH "" FORCE)
set(LIB_EAY_RELEASE "${LITE_DEPS_LIB}/libcrypto.lib" CACHE FILEPATH "" FORCE)
set(BUILD_SHARED_LIBS OFF)
set(ENET_NO_INSTALL ON CACHE BOOL "Do not install the embedded ENet library")
set(USE_MBEDTLS OFF CACHE BOOL "Use the existing OpenSSL dependency")
include(CTest)

# The host-control transport is opt-in until the matching curl source has been
# prepared and the Qt NvHTTP owner is migrated. Never download during CMake.
option(LITE_ENABLE_NATIVE_HTTP "Build the native host HTTP transport" OFF)
if(LITE_ENABLE_NATIVE_HTTP)
    add_subdirectory(host-http)
endif()

# Match the existing Windows qmake hardening and symbol generation.
add_compile_options(/utf-8 /Zi /guard:cf /guard:ehcont)
add_link_options(/DEBUG /guard:cf /guard:ehcont /CETCOMPAT)

add_subdirectory("${LITE_SOURCE_ROOT}/moonlight-common-c/moonlight-common-c" common-c)
add_library(lite-native-pairing STATIC "${LITE_SOURCE_ROOT}/app/backend/nativepairing.cpp")
add_library(lite-native-identity STATIC "${LITE_SOURCE_ROOT}/app/backend/nativeidentity.cpp")
target_compile_features(lite-native-identity PUBLIC cxx_std_17)
target_compile_options(lite-native-identity PRIVATE /W3 /WX)
target_include_directories(lite-native-identity PUBLIC "${LITE_SOURCE_ROOT}/app" PRIVATE "${LITE_DEPS_ROOT}/include/x64")
target_link_libraries(lite-native-identity PUBLIC "${LITE_DEPS_LIB}/libcrypto.lib")
target_compile_features(lite-native-pairing PUBLIC cxx_std_17)
target_compile_options(lite-native-pairing PRIVATE /W3 /WX)
target_include_directories(lite-native-pairing PUBLIC "${LITE_SOURCE_ROOT}/app" PRIVATE "${LITE_DEPS_ROOT}/include/x64")
target_link_libraries(lite-native-pairing PUBLIC "${LITE_DEPS_LIB}/libcrypto.lib")
# The embedded project selects debug macros using CMAKE_BUILD_TYPE. Visual
# Studio is multi-config, so select these per configuration without patching it.
get_target_property(common_definitions moonlight-common-c COMPILE_DEFINITIONS)
list(REMOVE_ITEM common_definitions NDEBUG LC_DEBUG)
set_property(TARGET moonlight-common-c PROPERTY COMPILE_DEFINITIONS "${common_definitions}")
target_compile_definitions(moonlight-common-c PRIVATE
    HAS_QOS_FLOWID=1 HAS_PQOS_FLOWID=1
    $<$<CONFIG:Debug>:LC_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)

add_library(h264bitstream STATIC
    "${LITE_SOURCE_ROOT}/h264bitstream/h264_nal.c"
    "${LITE_SOURCE_ROOT}/h264bitstream/h264_stream.c")
target_include_directories(h264bitstream SYSTEM PUBLIC "${LITE_SOURCE_ROOT}/h264bitstream")

add_library(AntiHooking SHARED "${LITE_SOURCE_ROOT}/AntiHooking/antihookingprotection.cpp")
target_compile_features(AntiHooking PRIVATE cxx_std_17)
target_compile_definitions(AntiHooking PRIVATE ANTIHOOKING_LIBRARY)
target_include_directories(AntiHooking PRIVATE "${LITE_DEPS_ROOT}/include")
target_link_libraries(AntiHooking PRIVATE "${LITE_DEPS_LIB}/detours.lib")

add_library(lite-cursor-shape STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/input/cursorshapeclassifier.cpp")
target_compile_features(lite-cursor-shape PUBLIC cxx_std_17)
target_compile_options(lite-cursor-shape PRIVATE /W3 /WX)
target_include_directories(lite-cursor-shape PUBLIC "${LITE_SOURCE_ROOT}/app")

# Use the same pinned SDL import library as the Qt build; do not discover a
# second SDL installation from the developer's environment.
add_library(lite-sdl2 SHARED IMPORTED)
set_target_properties(lite-sdl2 PROPERTIES
    IMPORTED_IMPLIB "${LITE_DEPS_LIB}/SDL2.lib"
    IMPORTED_LOCATION "${LITE_DEPS_LIB}/SDL2.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${LITE_DEPS_ROOT}/include/x64/SDL2")
add_library(lite-sdl-audio STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/audio/renderers/sdlaud.cpp")
target_compile_features(lite-sdl-audio PUBLIC cxx_std_17)
target_compile_options(lite-sdl-audio PRIVATE /W3 /WX)
target_include_directories(lite-sdl-audio PUBLIC "${LITE_SOURCE_ROOT}/app")
target_link_libraries(lite-sdl-audio PUBLIC moonlight-common-c lite-sdl2)

foreach(required IN ITEMS include/x64/SDL2/SDL_ttf.h lib/x64/SDL2_ttf.lib lib/x64/SDL2_ttf.dll)
    if(NOT EXISTS "${LITE_DEPS_ROOT}/${required}")
        message(FATAL_ERROR "Missing pinned SDL_ttf dependency: ${required}")
    endif()
endforeach()
add_library(lite-sdl-ttf SHARED IMPORTED)
set_target_properties(lite-sdl-ttf PROPERTIES
    IMPORTED_IMPLIB "${LITE_DEPS_LIB}/SDL2_ttf.lib"
    IMPORTED_LOCATION "${LITE_DEPS_LIB}/SDL2_ttf.dll")
target_link_libraries(lite-sdl-ttf INTERFACE lite-sdl2)
add_library(lite-overlay-text STATIC "${LITE_SOURCE_ROOT}/app/streaming/video/overlaymanager.cpp")
target_compile_features(lite-overlay-text PUBLIC cxx_std_17)
target_compile_options(lite-overlay-text PRIVATE /W3 /WX)
target_include_directories(lite-overlay-text PUBLIC "${LITE_SOURCE_ROOT}/app")
target_link_libraries(lite-overlay-text PUBLIC lite-sdl-ttf)

add_library(lite-opus SHARED IMPORTED)
set_target_properties(lite-opus PROPERTIES
    IMPORTED_IMPLIB "${LITE_DEPS_LIB}/opus.lib"
    IMPORTED_LOCATION "${LITE_DEPS_LIB}/opus.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${LITE_DEPS_ROOT}/include")
add_library(lite-audio-pipeline STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/audio/audiopipeline.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/audio/audiostream.cpp")
target_compile_features(lite-audio-pipeline PUBLIC cxx_std_17)
target_compile_options(lite-audio-pipeline PRIVATE /W3 /WX)
target_link_libraries(lite-audio-pipeline PUBLIC lite-sdl-audio lite-opus)

add_library(lite-stream-utils STATIC "${LITE_SOURCE_ROOT}/app/streaming/streamutils.cpp")
target_compile_features(lite-stream-utils PUBLIC cxx_std_17)
target_compile_options(lite-stream-utils PRIVATE /W3 /WX)
target_include_directories(lite-stream-utils PUBLIC "${LITE_SOURCE_ROOT}/app")
target_link_libraries(lite-stream-utils PUBLIC lite-sdl2 user32)
# Match the Windows Lite Qt build's window-probing preference.
target_compile_definitions(lite-stream-utils PRIVATE HAVE_LIBPLACEBO_VULKAN)

add_library(lite-stream-assets STATIC "${LITE_SOURCE_ROOT}/app/streaming/windowsstreamassets.cpp")
target_compile_features(lite-stream-assets PUBLIC cxx_std_17)
target_compile_options(lite-stream-assets PRIVATE /W3 /WX)
target_include_directories(lite-stream-assets PUBLIC "${LITE_SOURCE_ROOT}/app")
# Attach the resource to the executable that consumes the static library.
target_sources(lite-stream-assets INTERFACE "${LITE_SOURCE_ROOT}/app/streaming/windowsstreamassets.rc")

add_library(lite-clipboard-frame STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/clipboardframe.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/clipboardpipelines.cpp")
target_compile_features(lite-clipboard-frame PUBLIC cxx_std_17)
target_compile_options(lite-clipboard-frame PRIVATE /W3 /WX)
target_include_directories(lite-clipboard-frame PUBLIC "${LITE_SOURCE_ROOT}/app")

add_library(lite-clipboard-listener STATIC "${LITE_SOURCE_ROOT}/app/streaming/windowsclipboardlistener.cpp")
target_compile_features(lite-clipboard-listener PUBLIC cxx_std_17)
target_compile_options(lite-clipboard-listener PRIVATE /W3 /WX)
target_include_directories(lite-clipboard-listener PUBLIC "${LITE_SOURCE_ROOT}/app")
target_link_libraries(lite-clipboard-listener PUBLIC user32)

add_library(lite-clipboard-png STATIC "${LITE_SOURCE_ROOT}/app/streaming/windowsclipboardpng.cpp")
target_compile_features(lite-clipboard-png PUBLIC cxx_std_17)
target_compile_options(lite-clipboard-png PRIVATE /W3 /WX)
target_include_directories(lite-clipboard-png PUBLIC "${LITE_SOURCE_ROOT}/app")
target_link_libraries(lite-clipboard-png PUBLIC windowscodecs ole32)

add_library(lite-worker-dispatch STATIC "${LITE_SOURCE_ROOT}/app/backend/windowsworkerdispatch.cpp")
target_compile_features(lite-worker-dispatch PUBLIC cxx_std_17)
target_compile_options(lite-worker-dispatch PRIVATE /W3 /WX)
target_include_directories(lite-worker-dispatch PUBLIC "${LITE_SOURCE_ROOT}/app")
target_link_libraries(lite-worker-dispatch PUBLIC user32)

add_library(lite-touch-input STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/input/abstouch.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/input/reltouch.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/input/winpen.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/input/wintouchpad.cpp")
target_compile_features(lite-touch-input PUBLIC cxx_std_17)
target_compile_options(lite-touch-input PRIVATE /W3 /WX)
target_compile_definitions(lite-touch-input PUBLIC HAVE_WINDOWS_PEN_INPUT HAVE_WINDOWS_RAW_TOUCHPAD
    PRIVATE NOMINMAX)
target_link_libraries(lite-touch-input PUBLIC lite-stream-utils moonlight-common-c comctl32 hid)

add_library(lite-display-brightness STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/windowsdisplaybrightness.cpp")
target_compile_features(lite-display-brightness PUBLIC cxx_std_17)
target_compile_options(lite-display-brightness PRIVATE /W3 /WX)
target_compile_definitions(lite-display-brightness PRIVATE NOMINMAX)
target_include_directories(lite-display-brightness PUBLIC "${LITE_SOURCE_ROOT}/app")
target_link_libraries(lite-display-brightness PUBLIC lite-sdl2 dxgi user32)

add_library(lite-input-events STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/input/input.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/input/gamepad.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/input/keyboard.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/input/mouse.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/input/touchpad.cpp")
target_compile_features(lite-input-events PUBLIC cxx_std_17)
target_compile_options(lite-input-events PRIVATE /W3 /WX)
target_compile_definitions(lite-input-events PRIVATE NOMINMAX HAVE_PHYSICAL_DS5_HAPTICS)
target_link_libraries(lite-input-events PUBLIC lite-touch-input lite-cursor-shape)

foreach(required IN ITEMS include/x64/libavutil/frame.h lib/x64/avutil.lib lib/x64/avutil-61.dll
                          include/x64/libavcodec/avcodec.h lib/x64/avcodec.lib lib/x64/avcodec-63.dll)
    if(NOT EXISTS "${LITE_DEPS_ROOT}/${required}")
        message(FATAL_ERROR "Missing pinned FFmpeg dependency: ${required}")
    endif()
endforeach()
add_library(lite-avutil SHARED IMPORTED)
set_target_properties(lite-avutil PROPERTIES
    IMPORTED_IMPLIB "${LITE_DEPS_LIB}/avutil.lib"
    IMPORTED_LOCATION "${LITE_DEPS_LIB}/avutil-61.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${LITE_DEPS_ROOT}/include/x64")
add_library(lite-video-pacer STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg-renderers/pacer/pacer.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg-renderers/pacer/dxvsyncsource.cpp")
target_compile_features(lite-video-pacer PUBLIC cxx_std_17)
target_compile_options(lite-video-pacer PRIVATE /W3 /WX)
target_link_libraries(lite-video-pacer PUBLIC lite-stream-utils lite-avutil moonlight-common-c gdi32 user32)
add_library(lite-avcodec SHARED IMPORTED)
set_target_properties(lite-avcodec PROPERTIES
    IMPORTED_IMPLIB "${LITE_DEPS_LIB}/avcodec.lib"
    IMPORTED_LOCATION "${LITE_DEPS_LIB}/avcodec-63.dll")
target_link_libraries(lite-avcodec INTERFACE lite-avutil)
add_library(lite-video-buffer STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/video/decodebuffer.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/video/av1obu.cpp")
target_compile_features(lite-video-buffer PUBLIC cxx_std_17)
target_compile_options(lite-video-buffer PRIVATE /W3 /WX)
target_include_directories(lite-video-buffer PUBLIC "${LITE_SOURCE_ROOT}/app")
target_link_libraries(lite-video-buffer PUBLIC moonlight-common-c lite-sdl2 lite-avutil h264bitstream)

foreach(required IN ITEMS include/x64/libswscale/swscale.h lib/x64/swscale.lib lib/x64/swscale-10.dll)
    if(NOT EXISTS "${LITE_DEPS_ROOT}/${required}")
        message(FATAL_ERROR "Missing pinned swscale dependency: ${required}")
    endif()
endforeach()
add_library(lite-swscale SHARED IMPORTED)
set_target_properties(lite-swscale PROPERTIES
    IMPORTED_IMPLIB "${LITE_DEPS_LIB}/swscale.lib"
    IMPORTED_LOCATION "${LITE_DEPS_LIB}/swscale-10.dll")
target_link_libraries(lite-swscale INTERFACE lite-avutil)
add_library(lite-sdl-video STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg-renderers/sdlvid.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg-renderers/swframemapper.cpp")
target_compile_features(lite-sdl-video PUBLIC cxx_std_17)
target_compile_options(lite-sdl-video PRIVATE /W3 /WX)
target_link_libraries(lite-sdl-video PUBLIC lite-stream-utils lite-swscale moonlight-common-c)

# Keep the same embedded AMF implementation as app.pro. No SDK download or
# alternate vendor library is introduced by the native renderer build.
set(LITE_AMF_ROOT "${LITE_SOURCE_ROOT}/third-party/AMF/amf")
add_library(lite-amf STATIC
    "${LITE_AMF_ROOT}/public/common/AMFFactory.cpp"
    "${LITE_AMF_ROOT}/public/common/AMFSTL.cpp"
    "${LITE_AMF_ROOT}/public/common/Thread.cpp"
    "${LITE_AMF_ROOT}/public/common/TraceAdapter.cpp"
    "${LITE_AMF_ROOT}/public/common/Windows/ThreadWindows.cpp")
target_compile_features(lite-amf PUBLIC cxx_std_17)
target_include_directories(lite-amf SYSTEM PUBLIC "${LITE_AMF_ROOT}")
add_library(lite-d3d11-video STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg-renderers/d3d11va.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/video/videoenhancement.cpp")
target_compile_features(lite-d3d11-video PUBLIC cxx_std_17)
target_compile_options(lite-d3d11-video PRIVATE /W3 /WX)
# The inherited renderer already narrows texture indices, viewport floats and
# HDR fixed-point metadata in the Qt build. Preserve those conversions here;
# do not combine a numeric-behavior rewrite with dependency removal.
set_source_files_properties(
    "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg-renderers/d3d11va.cpp"
    PROPERTIES COMPILE_OPTIONS /wd4244)
target_link_libraries(lite-d3d11-video PUBLIC lite-stream-utils lite-avcodec lite-amf moonlight-common-c d3d11 dxgi dwmapi)

add_library(lite-dxva2-video STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg-renderers/dxva2.cpp")
target_compile_features(lite-dxva2-video PUBLIC cxx_std_17)
target_compile_options(lite-dxva2-video PRIVATE /W3 /WX)
target_compile_definitions(lite-dxva2-video PRIVATE _CRT_SECURE_NO_WARNINGS)
# Preserve the inherited overlay vertex int-to-float conversions, as above.
set_source_files_properties(
    "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg-renderers/dxva2.cpp"
    PROPERTIES COMPILE_OPTIONS /wd4244)
target_link_libraries(lite-dxva2-video PUBLIC lite-stream-utils lite-avcodec moonlight-common-c d3d9 dxva2 dwmapi)

add_library(lite-native-notification STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/video/nativenotification.cpp")
target_compile_features(lite-native-notification PUBLIC cxx_std_17)
target_compile_options(lite-native-notification PRIVATE /W3 /WX)
target_include_directories(lite-native-notification PUBLIC "${LITE_SOURCE_ROOT}/app")
target_link_libraries(lite-native-notification PUBLIC lite-sdl2 d3d11 dxgi dcomp d2d1 dwrite user32)

foreach(required IN ITEMS include/x64/libplacebo/vulkan.h include/vulkan/vulkan.h
                          lib/x64/libplacebo.lib lib/x64/libplacebo-374.dll)
    if(NOT EXISTS "${LITE_DEPS_ROOT}/${required}")
        message(FATAL_ERROR "Missing pinned libplacebo dependency: ${required}")
    endif()
endforeach()
add_library(lite-placebo SHARED IMPORTED)
set_target_properties(lite-placebo PROPERTIES
    IMPORTED_IMPLIB "${LITE_DEPS_LIB}/libplacebo.lib"
    IMPORTED_LOCATION "${LITE_DEPS_LIB}/libplacebo-374.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${LITE_DEPS_ROOT}/include")
add_library(lite-vulkan-video STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg-renderers/plvk.cpp"
    "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg-renderers/plvk_c.c")
target_compile_features(lite-vulkan-video PUBLIC cxx_std_17)
target_compile_definitions(lite-vulkan-video PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(lite-vulkan-video PRIVATE /W3 /WX)
# Preserve existing overlay coordinate conversions and the FFmpeg queue-lock
# callbacks required by this pinned production integration.
set_source_files_properties(
    "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg-renderers/plvk.cpp"
    PROPERTIES COMPILE_OPTIONS "/wd4244;/wd4996")
target_link_libraries(lite-vulkan-video PUBLIC lite-stream-utils lite-avcodec lite-placebo moonlight-common-c)

add_library(lite-generic-hwaccel STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg-renderers/genhwaccel.cpp")
target_compile_features(lite-generic-hwaccel PUBLIC cxx_std_17)
target_compile_definitions(lite-generic-hwaccel PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(lite-generic-hwaccel PRIVATE /W3 /WX)
target_link_libraries(lite-generic-hwaccel PUBLIC lite-stream-utils lite-avcodec moonlight-common-c)

add_library(lite-ffmpeg-decoder STATIC "${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg.cpp")
target_compile_features(lite-ffmpeg-decoder PUBLIC cxx_std_17)
target_compile_definitions(lite-ffmpeg-decoder PUBLIC HAVE_LIBPLACEBO_VULKAN)
target_compile_definitions(lite-ffmpeg-decoder PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(lite-ffmpeg-decoder PRIVATE /W3 /WX)
# The inherited first-pass switch has only a default when EGL is not built.
set_source_files_properties("${LITE_SOURCE_ROOT}/app/streaming/video/ffmpeg.cpp"
    PROPERTIES COMPILE_OPTIONS /wd4065)
target_link_libraries(lite-ffmpeg-decoder PUBLIC lite-d3d11-video lite-dxva2-video
    lite-vulkan-video lite-generic-hwaccel lite-sdl-video lite-video-buffer lite-video-pacer lite-overlay-text)

add_library(lite-decoder-factory STATIC "${LITE_SOURCE_ROOT}/app/streaming/video/decoderfactory.cpp")
target_compile_features(lite-decoder-factory PUBLIC cxx_std_17)
target_compile_options(lite-decoder-factory PRIVATE /W3 /WX)
target_compile_definitions(lite-decoder-factory PRIVATE HAVE_FFMPEG)
target_link_libraries(lite-decoder-factory PUBLIC lite-ffmpeg-decoder)

if(BUILD_TESTING)
    add_executable(windows_worker_dispatch "${LITE_SOURCE_ROOT}/tests/windows_worker_dispatch/main.cpp")
    target_link_libraries(windows_worker_dispatch PRIVATE lite-worker-dispatch)
    add_test(NAME windows_worker_dispatch COMMAND windows_worker_dispatch)

    add_executable(windows_clipboard_png "${LITE_SOURCE_ROOT}/tests/clipboard_frame/windows_clipboard_png.cpp")
    target_link_libraries(windows_clipboard_png PRIVATE lite-clipboard-png)
    add_test(NAME windows_clipboard_png COMMAND windows_clipboard_png)

    add_executable(clipboard_frame "${LITE_SOURCE_ROOT}/tests/clipboard_frame/main.cpp")
    target_compile_options(clipboard_frame PRIVATE /W3 /WX)
    target_link_libraries(clipboard_frame PRIVATE lite-clipboard-frame)
    add_test(NAME clipboard_frame COMMAND clipboard_frame)
    add_executable(reachability_policy "${LITE_SOURCE_ROOT}/tests/reachability_policy/main.cpp")
    target_compile_features(reachability_policy PRIVATE cxx_std_17)
    target_compile_options(reachability_policy PRIVATE /W3 /WX)
    add_test(NAME reachability_policy COMMAND reachability_policy)

    add_executable(native_pairing "${LITE_SOURCE_ROOT}/tests/native_pairing/main.cpp")
    add_executable(native_identity "${LITE_SOURCE_ROOT}/tests/native_identity/main.cpp")
    target_include_directories(native_identity PRIVATE "${LITE_DEPS_ROOT}/include/x64")
    target_compile_options(native_identity PRIVATE /W3 /WX)
    target_link_libraries(native_identity PRIVATE lite-native-identity)
    add_test(NAME native_identity COMMAND native_identity)
    set_tests_properties(native_identity PROPERTIES TIMEOUT 15)
    target_include_directories(native_pairing PRIVATE "${LITE_DEPS_ROOT}/include/x64")
    target_compile_options(native_pairing PRIVATE /W3 /WX)
    target_link_libraries(native_pairing PRIVATE lite-native-pairing)
    add_test(NAME native_pairing COMMAND native_pairing "${LITE_SOURCE_ROOT}/tests/clipboard_payload_routing/fixtures")
    set_tests_properties(native_pairing PROPERTIES TIMEOUT 15)
    # The pinned fork's decode fixture omits stopVideoDepacketizer() before
    # destroying a used queue. Patch a build-directory copy only: the fork is
    # read-only to this repository's maintainer, so its gitlink stays unchanged.
    find_package(Git REQUIRED)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/patches/common-c-decode-test-shutdown.patch")
    set(decode_fixture_root "${CMAKE_CURRENT_BINARY_DIR}/common-c-test-fixture")
    file(MAKE_DIRECTORY "${decode_fixture_root}/tests")
    configure_file(
        "${LITE_SOURCE_ROOT}/moonlight-common-c/moonlight-common-c/tests/PyrowaveDecodeUnitTest.c"
        "${decode_fixture_root}/tests/PyrowaveDecodeUnitTest.c" COPYONLY)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
            # Prevent Git from treating the fixture as a subdirectory of this
            # checkout and silently skipping paths outside its working prefix.
            "GIT_CEILING_DIRECTORIES=${CMAKE_CURRENT_BINARY_DIR}"
            "${GIT_EXECUTABLE}" apply --no-index
            "${CMAKE_CURRENT_SOURCE_DIR}/patches/common-c-decode-test-shutdown.patch"
        WORKING_DIRECTORY "${decode_fixture_root}"
        RESULT_VARIABLE patch_result ERROR_VARIABLE patch_error)
    if(NOT patch_result EQUAL 0)
        message(FATAL_ERROR "Cannot apply the pinned common-c test fixture fix: ${patch_error}")
    endif()
    # The fixture's ../src includes resolve against the existing src include
    # directory. All production code and data still come from the submodule.
    set_property(TARGET pyrowave-decode-unit-tests PROPERTY SOURCES
        "${decode_fixture_root}/tests/PyrowaveDecodeUnitTest.c")
    target_include_directories(pyrowave-decode-unit-tests PRIVATE
        "${LITE_SOURCE_ROOT}/moonlight-common-c/moonlight-common-c/tests")
    target_sources(pyrowave-decode-unit-tests PRIVATE
        "${LITE_SOURCE_ROOT}/tests/lite_native/assert_reporting.c")
    add_executable(lite-native-link-test "${LITE_SOURCE_ROOT}/tests/lite_native/main.c")
    target_link_libraries(lite-native-link-test PRIVATE moonlight-common-c h264bitstream)
    add_test(NAME lite-native-link-test COMMAND lite-native-link-test)
    set_tests_properties(lite-native-link-test PROPERTIES TIMEOUT 15)

    add_executable(overlay_text "${LITE_SOURCE_ROOT}/tests/overlay_text/main.cpp")
    target_compile_definitions(overlay_text PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(overlay_text PRIVATE /W3 /WX)
    target_link_libraries(overlay_text PRIVATE lite-overlay-text lite-stream-assets)
    add_test(NAME overlay_text COMMAND overlay_text "${LITE_SOURCE_ROOT}/app/ModeSeven.ttf")
    set_tests_properties(overlay_text PROPERTIES TIMEOUT 15)

    add_executable(d3d11_video_renderer "${LITE_SOURCE_ROOT}/tests/d3d11_video_renderer/main.cpp")
    target_compile_definitions(d3d11_video_renderer PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(d3d11_video_renderer PRIVATE /W3 /WX)
    target_link_libraries(d3d11_video_renderer PRIVATE lite-d3d11-video lite-stream-assets)
    add_test(NAME d3d11_video_renderer COMMAND d3d11_video_renderer)
    set_tests_properties(d3d11_video_renderer PROPERTIES TIMEOUT 15)

    add_executable(dxva2_video_renderer "${LITE_SOURCE_ROOT}/tests/dxva2_video_renderer/main.cpp")
    target_compile_definitions(dxva2_video_renderer PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(dxva2_video_renderer PRIVATE /W3 /WX)
    target_link_libraries(dxva2_video_renderer PRIVATE lite-dxva2-video)
    add_test(NAME dxva2_video_renderer COMMAND dxva2_video_renderer)
    set_tests_properties(dxva2_video_renderer PROPERTIES TIMEOUT 15)

    add_executable(vulkan_video_renderer "${LITE_SOURCE_ROOT}/tests/vulkan_video_renderer/main.cpp")
    target_compile_definitions(vulkan_video_renderer PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(vulkan_video_renderer PRIVATE /W3 /WX)
    target_link_libraries(vulkan_video_renderer PRIVATE lite-vulkan-video lite-generic-hwaccel)
    add_test(NAME vulkan_video_renderer COMMAND vulkan_video_renderer)
    set_tests_properties(vulkan_video_renderer PROPERTIES TIMEOUT 15)

    add_executable(ffmpeg_video_decoder "${LITE_SOURCE_ROOT}/tests/ffmpeg_video_decoder/main.cpp")
    target_compile_definitions(ffmpeg_video_decoder PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(ffmpeg_video_decoder PRIVATE /W3 /WX)
    target_link_libraries(ffmpeg_video_decoder PRIVATE lite-decoder-factory)
    add_test(NAME ffmpeg_video_decoder COMMAND ffmpeg_video_decoder "${LITE_SOURCE_ROOT}")
    set_tests_properties(ffmpeg_video_decoder PROPERTIES TIMEOUT 15)

    add_executable(native_notification "${LITE_SOURCE_ROOT}/tests/native_notification/main.cpp")
    # Manual visible GPUI/SDL focus fixture; intentionally not an unattended ctest.
    add_executable(gpui_control_owner "${LITE_SOURCE_ROOT}/tests/gpui_control_owner/main.cpp")
    target_compile_definitions(gpui_control_owner PRIVATE SDL_MAIN_HANDLED)
    target_compile_features(gpui_control_owner PRIVATE cxx_std_17)
    target_compile_options(gpui_control_owner PRIVATE /W3 /WX)
    target_link_libraries(gpui_control_owner PRIVATE lite-sdl2 user32)
    set_target_properties(gpui_control_owner PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(gpui_control_owner PRIVATE /ENTRY:mainCRTStartup)
    target_compile_definitions(native_notification PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(native_notification PRIVATE /W3 /WX)
    target_link_libraries(native_notification PRIVATE lite-native-notification)
    # GPU/desktop composition is explicit, never required by headless CTest.

    # These state/placement tests compile the production helpers directly and have
    # no Qt, SDL window, streaming host, or user-data dependency.
    foreach(input_test IN ITEMS keyboard_state gamepad_sensor_rate pen_history_selection
                                overlay_placement_axis overlay_event_wake_state overlay_toast_event_state
                                touchpad_contacts)
        add_executable(${input_test} "${LITE_SOURCE_ROOT}/tests/${input_test}/main.cpp")
        target_compile_features(${input_test} PRIVATE cxx_std_17)
        target_compile_options(${input_test} PRIVATE /W3 /WX)
        target_include_directories(${input_test} PRIVATE "${LITE_SOURCE_ROOT}/app"
            "${LITE_SOURCE_ROOT}/moonlight-common-c/moonlight-common-c/src")
        add_test(NAME ${input_test} COMMAND ${input_test})
        set_tests_properties(${input_test} PROPERTIES TIMEOUT 15)
    endforeach()

    add_executable(native_input_owner "${LITE_SOURCE_ROOT}/tests/native_input_owner/main.cpp")
    target_compile_definitions(native_input_owner PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(native_input_owner PRIVATE /W3 /WX)
    target_link_libraries(native_input_owner PRIVATE lite-input-events)
    add_test(NAME native_input_owner COMMAND native_input_owner)
    set_tests_properties(native_input_owner PROPERTIES TIMEOUT 15)

    add_executable(cursor_shape_classification
        "${LITE_SOURCE_ROOT}/tests/cursor_shape_classification/main.cpp")
    target_link_libraries(cursor_shape_classification PRIVATE lite-cursor-shape)
    target_compile_options(cursor_shape_classification PRIVATE /W3 /WX)
    add_test(NAME cursor_shape_classification COMMAND cursor_shape_classification)
    set_tests_properties(cursor_shape_classification PROPERTIES TIMEOUT 15)

    add_executable(sdl_audio_renderer
        "${LITE_SOURCE_ROOT}/tests/sdl_audio_renderer/main.cpp")
    target_compile_definitions(sdl_audio_renderer PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(sdl_audio_renderer PRIVATE /W3 /WX)
    target_link_libraries(sdl_audio_renderer PRIVATE lite-sdl-audio)
    add_test(NAME sdl_audio_renderer COMMAND sdl_audio_renderer)
    set_tests_properties(sdl_audio_renderer PROPERTIES TIMEOUT 15)

    add_executable(stream_utils "${LITE_SOURCE_ROOT}/tests/stream_utils/main.cpp")
    target_compile_definitions(stream_utils PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(stream_utils PRIVATE /W3 /WX)
    target_link_libraries(stream_utils PRIVATE lite-stream-utils)
    add_test(NAME stream_utils COMMAND stream_utils)
    set_tests_properties(stream_utils PROPERTIES TIMEOUT 15)

    add_executable(audio_pipeline "${LITE_SOURCE_ROOT}/tests/audio_pipeline/main.cpp")
    target_compile_definitions(audio_pipeline PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(audio_pipeline PRIVATE /W3 /WX)
    target_link_libraries(audio_pipeline PRIVATE lite-audio-pipeline)
    add_test(NAME audio_pipeline COMMAND audio_pipeline)
    set_tests_properties(audio_pipeline PROPERTIES TIMEOUT 15)

    add_executable(video_pacer "${LITE_SOURCE_ROOT}/tests/video_pacer/main.cpp")
    target_compile_definitions(video_pacer PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(video_pacer PRIVATE /W3 /WX)
    target_link_libraries(video_pacer PRIVATE lite-video-pacer)
    add_test(NAME video_pacer COMMAND video_pacer)
    set_tests_properties(video_pacer PROPERTIES TIMEOUT 15)

    add_executable(video_decode_path "${LITE_SOURCE_ROOT}/tests/video_decode_path/main.cpp")
    target_compile_definitions(video_decode_path PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(video_decode_path PRIVATE /W3 /WX)
    target_link_libraries(video_decode_path PRIVATE lite-video-buffer lite-video-pacer lite-avcodec)
    add_test(NAME video_decode_path COMMAND video_decode_path)
    set_tests_properties(video_decode_path PROPERTIES TIMEOUT 15)

    add_executable(sdl_video_renderer "${LITE_SOURCE_ROOT}/tests/sdl_video_renderer/main.cpp")
    target_compile_definitions(sdl_video_renderer PRIVATE SDL_MAIN_HANDLED)
    target_compile_options(sdl_video_renderer PRIVATE /W3 /WX)
    target_link_libraries(sdl_video_renderer PRIVATE lite-sdl-video lite-video-pacer lite-video-buffer lite-avcodec)
    add_test(NAME sdl_video_renderer COMMAND sdl_video_renderer)
    set_tests_properties(sdl_video_renderer PROPERTIES TIMEOUT 15)
endif()
