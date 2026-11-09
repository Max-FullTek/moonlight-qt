cmake_minimum_required(VERSION 3.24)
project(moonlight-lite-native LANGUAGES C CXX)

# This is the native-library stage, not the Qt-free streaming worker yet.
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    message(FATAL_ERROR "Windows Lite currently requires the MSVC Visual Studio x64 generator")
endif()

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

# Match the existing Windows qmake hardening and symbol generation.
add_compile_options(/utf-8 /Zi /guard:cf /guard:ehcont)
add_link_options(/DEBUG /guard:cf /guard:ehcont /CETCOMPAT)

add_subdirectory("${LITE_SOURCE_ROOT}/moonlight-common-c/moonlight-common-c" common-c)
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

add_library(lite-opus SHARED IMPORTED)
set_target_properties(lite-opus PROPERTIES
    IMPORTED_IMPLIB "${LITE_DEPS_LIB}/opus.lib"
    IMPORTED_LOCATION "${LITE_DEPS_LIB}/opus.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${LITE_DEPS_ROOT}/include")
add_library(lite-audio-pipeline STATIC
    "${LITE_SOURCE_ROOT}/app/streaming/audio/audiopipeline.cpp")
target_compile_features(lite-audio-pipeline PUBLIC cxx_std_17)
target_compile_options(lite-audio-pipeline PRIVATE /W3 /WX)
target_link_libraries(lite-audio-pipeline PUBLIC lite-sdl-audio lite-opus)

add_library(lite-stream-utils STATIC "${LITE_SOURCE_ROOT}/app/streaming/streamutils.cpp")
target_compile_features(lite-stream-utils PUBLIC cxx_std_17)
target_compile_options(lite-stream-utils PRIVATE /W3 /WX)
target_include_directories(lite-stream-utils PUBLIC "${LITE_SOURCE_ROOT}/app")
target_link_libraries(lite-stream-utils PUBLIC lite-sdl2)
# Match the Windows Lite Qt build's window-probing preference.
target_compile_definitions(lite-stream-utils PRIVATE HAVE_LIBPLACEBO_VULKAN)

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

if(BUILD_TESTING)
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
endif()
