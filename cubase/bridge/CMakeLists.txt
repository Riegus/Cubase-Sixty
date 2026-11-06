cmake_minimum_required(VERSION 3.20)
project(cubase_sixty_bridge CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The USB drivers and the wire-protocol code are reused unchanged from the
# Rea-Sixty extension (see ../docs/extension-survey.md, section A).
set(EXT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../extension/src")

# ----- libusb ---------------------------------------------------------------
# Windows: download the libusb release (https://github.com/libusb/libusb/releases),
# unzip it, and pass -DLIBUSB_ROOT=<folder> (the one containing include/ and VS2022/).
# Linux/macOS: pkg-config finds it.
if(WIN32)
    if(NOT LIBUSB_ROOT)
        message(FATAL_ERROR "Set -DLIBUSB_ROOT=<path to unzipped libusb release>")
    endif()
    set(LIBUSB_MSVC "VS2022" CACHE STRING "libusb release subfolder (VS2022, VS2019, ...)")
    add_library(cs_libusb INTERFACE)
    # The header's folder differs between releases: look for libusb.h anywhere under the root.
    file(GLOB_RECURSE _libusb_h "${LIBUSB_ROOT}/libusb.h")
    list(GET _libusb_h 0 _libusb_h0)
    if(NOT _libusb_h0)
        message(FATAL_ERROR "libusb.h not found anywhere under ${LIBUSB_ROOT}")
    endif()
    get_filename_component(_libusb_inc "${_libusb_h0}" DIRECTORY)
    message(STATUS "libusb.h found in ${_libusb_inc}")
    target_include_directories(cs_libusb INTERFACE "${_libusb_inc}")
    target_link_libraries(cs_libusb INTERFACE "${LIBUSB_ROOT}/${LIBUSB_MSVC}/MS64/dll/libusb-1.0.lib")
    set(LIBUSB_DLL "${LIBUSB_ROOT}/${LIBUSB_MSVC}/MS64/dll/libusb-1.0.dll")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
    add_library(cs_libusb INTERFACE)
    target_link_libraries(cs_libusb INTERFACE PkgConfig::LIBUSB)
endif()

# ----- RtMidi (virtual/loopback MIDI ports on Windows via WinMM) ------------
include(FetchContent)
FetchContent_Declare(rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG        6.0.0)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rtmidi)

add_executable(cubase_sixty_bridge
    src/main.cpp
    src/Mapping.h
    src/MidiPorts.cpp
    src/MidiPorts.h
    src/Uf1Bridge.cpp
    src/Uf1Bridge.h
    src/Uc1Bridge.cpp
    src/ScriptLog.cpp
    src/ScriptLog.h
    src/Uc1Bridge.h
    ${EXT_SRC}/UF8Device.cpp
    ${EXT_SRC}/UF1Device.cpp
    ${EXT_SRC}/UF1Protocol.cpp
    ${EXT_SRC}/Uf1Pacer.cpp
    ${EXT_SRC}/UC1Device.cpp
    ${EXT_SRC}/UC1Protocol.cpp
    ${EXT_SRC}/Protocol.cpp
    ${EXT_SRC}/Palette.cpp
    ${EXT_SRC}/LogPath.cpp)
target_include_directories(cubase_sixty_bridge PRIVATE src ${EXT_SRC})
target_link_libraries(cubase_sixty_bridge PRIVATE cs_libusb rtmidi)
if(MSVC)
    target_compile_options(cubase_sixty_bridge PRIVATE /utf-8 /permissive-)
endif()

# Pure mapping logic, testable without hardware or MIDI ports.
add_executable(test_mapping tests/test_mapping.cpp ${EXT_SRC}/Palette.cpp)
target_include_directories(test_mapping PRIVATE src ${EXT_SRC})

if(WIN32)
    add_custom_command(TARGET cubase_sixty_bridge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBUSB_DLL}" $<TARGET_FILE_DIR:cubase_sixty_bridge>)
endif()
