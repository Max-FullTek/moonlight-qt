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
                          lib/x64/libcrypto.lib lib/x64/libcrypto-3-x64.dll lib/x64/detours.lib)
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
target_include_directories(h264bitstream PUBLIC "${LITE_SOURCE_ROOT}/h264bitstream")

add_library(AntiHooking SHARED "${LITE_SOURCE_ROOT}/AntiHooking/antihookingprotection.cpp")
target_compile_features(AntiHooking PRIVATE cxx_std_17)
target_compile_definitions(AntiHooking PRIVATE ANTIHOOKING_LIBRARY)
target_include_directories(AntiHooking PRIVATE "${LITE_DEPS_ROOT}/include")
target_link_libraries(AntiHooking PRIVATE "${LITE_DEPS_LIB}/detours.lib")

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
endif()
