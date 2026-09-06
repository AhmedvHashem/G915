cmake_minimum_required(VERSION 3.17)

include(pico_sdk_import.cmake)

project(g915_feather_rp2040_usb_host_bridge VERSION 0.1.0 LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(BRIDGE_USB_CDC_DIAGNOSTICS
    "Add a USB CDC interface with live host-enumeration diagnostics" OFF)

if(BRIDGE_USB_CDC_DIAGNOSTICS)
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

# The G915 LIGHTSPEED receiver exposes three HID interfaces but intermittently
# stalls the optional GET_DESCRIPTOR(report) control request when connected
# through Pico-PIO-USB.  This bridge only consumes boot-keyboard reports, so a
# report descriptor is unnecessary.  Generate a project-local TinyUSB HID host
# source that completes mounting immediately instead of making that request.
# Keep the installed Pico SDK untouched and fail configuration if TinyUSB's
# implementation changes enough that this narrowly scoped patch no longer
# applies.
set(TINYUSB_HID_HOST_SOURCE
    "${PICO_TINYUSB_PATH}/src/class/hid/hid_host.c")
set(PATCHED_TINYUSB_DIR "${CMAKE_CURRENT_BINARY_DIR}/patched-tinyusb")
set(PATCHED_TINYUSB_HID_HOST
    "${PATCHED_TINYUSB_DIR}/hid_host_boot_protocol.c")
file(MAKE_DIRECTORY "${PATCHED_TINYUSB_DIR}")
file(READ "${TINYUSB_HID_HOST_SOURCE}" TINYUSB_HID_HOST_CONTENTS)

set(TINYUSB_REPORT_DESCRIPTOR_BLOCK [=[
      // Get Report Descriptor if possible
      // using usbh enumeration buffer since report descriptor can be very long
      if (p_hid->report_desc_len > CFG_TUH_ENUMERATION_BUFSIZE) {
        TU_LOG_DRV("HID Skip Report Descriptor since it is too large %u bytes\r\n", p_hid->report_desc_len);

        // Driver is mounted without report descriptor
        config_driver_mount_complete(daddr, idx, NULL, 0);
      } else {
        tuh_descriptor_get_hid_report(daddr, itf_num, p_hid->report_desc_type, 0,
                                      usbh_get_enum_buf(), p_hid->report_desc_len,
                                      process_set_config, CONFIG_COMPLETE);
      }
]=])
set(TINYUSB_BOOT_PROTOCOL_BLOCK [=[
      // This application consumes only boot-protocol keyboard reports.  Some
      // composite wireless receivers stall this optional descriptor request,
      // so mount without the report descriptor.
      TU_LOG_DRV("HID Skip Report Descriptor for boot-protocol bridge\r\n");
      config_driver_mount_complete(daddr, idx, NULL, 0);
]=])
string(FIND "${TINYUSB_HID_HOST_CONTENTS}"
    "${TINYUSB_REPORT_DESCRIPTOR_BLOCK}" TINYUSB_PATCH_OFFSET)
if(TINYUSB_PATCH_OFFSET EQUAL -1)
    message(FATAL_ERROR
        "Unable to apply the G915 TinyUSB HID-host workaround; "
        "the SDK's hid_host.c no longer contains the expected block")
endif()
string(REPLACE "${TINYUSB_REPORT_DESCRIPTOR_BLOCK}"
    "${TINYUSB_BOOT_PROTOCOL_BLOCK}"
    TINYUSB_HID_HOST_CONTENTS "${TINYUSB_HID_HOST_CONTENTS}")
file(WRITE "${PATCHED_TINYUSB_HID_HOST}" "${TINYUSB_HID_HOST_CONTENTS}")

get_target_property(TINYUSB_HOST_SOURCES tinyusb_host_base INTERFACE_SOURCES)
set(PATCHED_TINYUSB_HOST_SOURCES)
foreach(TINYUSB_HOST_SOURCE IN LISTS TINYUSB_HOST_SOURCES)
    if(NOT TINYUSB_HOST_SOURCE STREQUAL "${TINYUSB_HID_HOST_SOURCE}")
        list(APPEND PATCHED_TINYUSB_HOST_SOURCES "${TINYUSB_HOST_SOURCE}")
    endif()
endforeach()
list(APPEND PATCHED_TINYUSB_HOST_SOURCES "${PATCHED_TINYUSB_HID_HOST}")
set_property(TARGET tinyusb_host_base PROPERTY INTERFACE_SOURCES
    "${PATCHED_TINYUSB_HOST_SOURCES}")
target_include_directories(tinyusb_host_base INTERFACE
    "${PICO_TINYUSB_PATH}/src/class/hid")

set(PICO2_BRIDGE_DIR "${CMAKE_CURRENT_LIST_DIR}/../g915_pico2w_bridge")

set(BRIDGE_SOURCES
    main.c
    host/usb_host_keyboard.c
    usb_descriptors.c
    ${PICO2_BRIDGE_DIR}/core/keyboard_state.c
    ${PICO2_BRIDGE_DIR}/core/report_pipe.c
    ${PICO2_BRIDGE_DIR}/usb/usb_keyboard.c
)

add_executable(g915_feather_rp2040_usb_host_bridge ${BRIDGE_SOURCES})

if(BRIDGE_USB_CDC_DIAGNOSTICS)
    target_compile_definitions(g915_feather_rp2040_usb_host_bridge PRIVATE
        BRIDGE_USB_CDC_DIAGNOSTICS=1)
else()
    target_compile_definitions(g915_feather_rp2040_usb_host_bridge PRIVATE
        BRIDGE_USB_CDC_DIAGNOSTICS=0)
endif()

set_source_files_properties(${BRIDGE_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic"
)

# TinyUSB contains a 32-bit enum constant diagnosed by GCC under -Wpedantic.
set_property(SOURCE
    main.c
    host/usb_host_keyboard.c
    usb_descriptors.c
    ${PICO2_BRIDGE_DIR}/usb/usb_keyboard.c
    APPEND PROPERTY COMPILE_OPTIONS "-Wno-pedantic"
)

target_include_directories(g915_feather_rp2040_usb_host_bridge PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${PICO2_BRIDGE_DIR}
)

target_link_libraries(g915_feather_rp2040_usb_host_bridge PRIVATE
    pico_stdlib
    pico_unique_id
    pico_multicore
    pico_sync
    hardware_clocks
    hardware_gpio
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
