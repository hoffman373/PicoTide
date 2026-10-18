cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

set(PICO_BOARD "pico2" CACHE STRING "Target board")

# Optional overrides: shared settings from ~/.config/pico/local.cmake first,
# then the project's own local.cmake so its values win (entries use
# CACHE ... FORCE). They load before the SDK so a PICO_BOARD override applies.
# This firmware does not use WiFi or MQTT, so no credentials are needed.
foreach(_local "$ENV{HOME}/.config/pico/local.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/local.cmake")
    if(EXISTS "${_local}")
        include("${_local}")
    endif()
endforeach()

include(pico_sdk_import.cmake)

project(PicoTideFirmware C CXX ASM)

set(PICO_CXX_ENABLE_EXCEPTIONS 1)
set(PICO_CXX_ENABLE_RTTI 1)

pico_sdk_init()

# ---------------------------------------------------------------------------
# Bench tester (separate firmware)
# ---------------------------------------------------------------------------
add_executable(PicoTideBenchTester
    src/main.cpp
    src/card.cpp
    src/tests.cpp
)

target_include_directories(PicoTideBenchTester PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PicoTideBenchTester
    pico_stdlib
)

pico_set_program_name(PicoTideBenchTester "PicoTideBenchTester")
pico_set_program_version(PicoTideBenchTester "0.1")

pico_enable_stdio_usb(PicoTideBenchTester 1)
pico_enable_stdio_uart(PicoTideBenchTester 0)

pico_add_extra_outputs(PicoTideBenchTester)

# ---------------------------------------------------------------------------
# In-machine bus logger (separate firmware)
# ---------------------------------------------------------------------------
add_executable(PicoTideBusLogger
    src/buslog.cpp
    src/card.cpp
)

target_include_directories(PicoTideBusLogger PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PicoTideBusLogger
    pico_stdlib
    pico_multicore
)

pico_set_program_name(PicoTideBusLogger "PicoTideBusLogger")
pico_set_program_version(PicoTideBusLogger "0.1")

pico_enable_stdio_usb(PicoTideBusLogger 1)
pico_enable_stdio_uart(PicoTideBusLogger 0)

pico_add_extra_outputs(PicoTideBusLogger)

# ---------------------------------------------------------------------------
# Option ROM server (separate firmware): answers ISA memory reads with ROM_IMAGE
# ---------------------------------------------------------------------------
# No image is shipped: supply one as option-rom.bin in the project root, or
# point ROM_IMAGE at it. A relative path is taken from the project root.
set(ROM_IMAGE "option-rom.bin" CACHE FILEPATH "Option ROM image served by PicoTideOptionRom")
get_filename_component(ROM_IMAGE_PATH "${ROM_IMAGE}" ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT EXISTS "${ROM_IMAGE_PATH}")
    message(FATAL_ERROR "Option ROM image not found: ${ROM_IMAGE_PATH}\n"
        "Copy the image there, or configure with -DROM_IMAGE=<path>.")
endif()
set(ROM_IMAGE_CPP "${CMAKE_CURRENT_BINARY_DIR}/rom_image.cpp")
add_custom_command(
    OUTPUT ${ROM_IMAGE_CPP}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${ROM_IMAGE_PATH} -DOUTPUT=${ROM_IMAGE_CPP}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bin2c.cmake
    DEPENDS ${ROM_IMAGE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bin2c.cmake
    COMMENT "Embedding option ROM image"
)

add_executable(PicoTideOptionRom
    src/rom.cpp
    src/card.cpp
    ${ROM_IMAGE_CPP}
)

target_include_directories(PicoTideOptionRom PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# -DBUSROM_DEBUG=ON adds the bus read logging and RESET DRV diagnostics.
option(BUSROM_DEBUG "PicoTideOptionRom: log bus reads and RESET DRV pulses" OFF)
if(BUSROM_DEBUG)
    target_compile_definitions(PicoTideOptionRom PRIVATE BUSROM_DEBUG=1)
endif()

target_link_libraries(PicoTideOptionRom
    pico_stdlib
    pico_multicore
)

pico_set_program_name(PicoTideOptionRom "PicoTideOptionRom")
pico_set_program_version(PicoTideOptionRom "0.1")

pico_enable_stdio_usb(PicoTideOptionRom 1)
pico_enable_stdio_uart(PicoTideOptionRom 0)

pico_add_extra_outputs(PicoTideOptionRom)

# ---------------------------------------------------------------------------
# Code formatting
# ---------------------------------------------------------------------------
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMAT_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    )
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES}
        COMMENT "Running clang-format"
    )
endif()
