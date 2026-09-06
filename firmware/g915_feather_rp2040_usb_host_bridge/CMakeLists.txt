cmake_minimum_required(VERSION 3.17)

include(pico_sdk_import.cmake)

project(g915_feather_rp2040_usb_host_bridge VERSION 0.1.0 LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(BRIDGE_USB_CDC_DIAGNOSTICS
    "Add a USB CDC interface with live host-enumeration diagnostics" OFF)
# Verbose host logging calls vsnprintf on core 1 from inside the report path,
# which inflates the latency figures the diagnostic build reports. Keep it off
# unless enumeration itself is being debugged.
option(BRIDGE_USB_HOST_LOG
    "Enable verbose TinyUSB host logging (skews latency measurements)" OFF)

if(BRIDGE_USB_CDC_DIAGNOSTICS AND BRIDGE_USB_HOST_LOG)
    set(LOG 2 CACHE STRING "TinyUSB host diagnostic logging level" FORCE)
else()
    set(LOG 0 CACHE STRING "Disable TinyUSB logging in production" FORCE)
endif()

# TinyUSB's RP2040 dual-role support uses Pico-PIO-USB for the second root
# port. Pico SDK installations do not always include that optional source, so
# fetch a pinned release into the build tree before pico_sdk_init() discovers
# TinyUSB.
include(FetchContent)
FetchContent_Declare(
    pico_pio_usb
    GIT_REPOSITORY https://github.com/sekigon-gonnoc/Pico-PIO-USB.git
    # Includes upstream's reduced full-speed handshake-delay fix, which is
    # important for receivers with tight control-transfer turnaround timing.
    GIT_TAG 5a37a66dc5d3fbe0ef3cdbeda923a757440f984f
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake-do-not-add
)
FetchContent_MakeAvailable(pico_pio_usb)
set(PICO_PIO_USB_PATH "${pico_pio_usb_SOURCE_DIR}" CACHE PATH
    "Pico-PIO-USB source directory" FORCE)

pico_sdk_init()

# Keyboard state, the report pipe, and the USB keyboard adapter are shared
# with the Pico 2 W bridge and live in firmware/common.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common ${CMAKE_BINARY_DIR}/common)

# The receiver side does not use TinyUSB's generic HID host class. The
# application-defined driver in host/boot_keyboard_host.c claims only the
# receiver's boot-keyboard interface and never requests the HID report
# descriptor, which this receiver intermittently stalls through Pico-PIO-USB.
set(BRIDGE_SOURCES
    main.c
    host/boot_keyboard_host.c
    host/frame_phase.c
    host/sof_phase_lock.c
    host/usb_host_keyboard.c
    usb_descriptors.c
)

add_executable(g915_feather_rp2040_usb_host_bridge ${BRIDGE_SOURCES})

if(BRIDGE_USB_CDC_DIAGNOSTICS)
    target_compile_definitions(g915_feather_rp2040_usb_host_bridge PRIVATE
        BRIDGE_USB_CDC_DIAGNOSTICS=1)
else()
    target_compile_definitions(g915_feather_rp2040_usb_host_bridge PRIVATE
        BRIDGE_USB_CDC_DIAGNOSTICS=0)
endif()

if(BRIDGE_USB_HOST_LOG)
    target_compile_definitions(g915_feather_rp2040_usb_host_bridge PRIVATE
        BRIDGE_USB_HOST_LOG_LEVEL=2)
else()
    target_compile_definitions(g915_feather_rp2040_usb_host_bridge PRIVATE
        BRIDGE_USB_HOST_LOG_LEVEL=0)
endif()

set_source_files_properties(${BRIDGE_SOURCES} ${G915_BRIDGE_COMMON_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic"
)

# TinyUSB contains a 32-bit enum constant diagnosed by GCC under -Wpedantic.
set_property(SOURCE
    main.c
    host/boot_keyboard_host.c
    host/usb_host_keyboard.c
    usb_descriptors.c
    ${G915_BRIDGE_COMMON_TUSB_SOURCES}
    APPEND PROPERTY COMPILE_OPTIONS "-Wno-pedantic"
)

target_include_directories(g915_feather_rp2040_usb_host_bridge PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(g915_feather_rp2040_usb_host_bridge PRIVATE
    g915_bridge_common
    pico_stdlib
    pico_unique_id
    pico_multicore
    pico_sync
    hardware_clocks
    hardware_gpio
    hardware_sync
    hardware_timer
    tinyusb_device
    tinyusb_host
    tinyusb_pico_pio_usb
)

# Native USB belongs exclusively to the keyboard device stack. Diagnostics are
# available on UART0 (TX GPIO0, RX GPIO1).
pico_enable_stdio_usb(g915_feather_rp2040_usb_host_bridge 0)
pico_enable_stdio_uart(g915_feather_rp2040_usb_host_bridge 1)

pico_set_program_name(g915_feather_rp2040_usb_host_bridge
    "G915 Feather PS5 Bridge")
pico_set_program_version(g915_feather_rp2040_usb_host_bridge
    "${PROJECT_VERSION}")
pico_add_extra_outputs(g915_feather_rp2040_usb_host_bridge)
