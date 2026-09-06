cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(g915_pico2w_bridge VERSION 0.3.0 LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Keyboard state, the report pipe, and the USB keyboard adapter are shared
# with the Feather bridge and live in firmware/common.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common ${CMAKE_BINARY_DIR}/common)

set(BRIDGE_SOURCES
    main.c
    ble/hid_keyboard_decoder.c
    ble/hog_client.c
    usb_descriptors.c
)

add_executable(g915_pico2w_bridge ${BRIDGE_SOURCES})

# The polling CYW43 target does not select an lwIP mode. This bridge uses
# Bluetooth only, so provide the same no-lwIP policy explicitly rather than
# linking an unused TCP/IP stack.
target_compile_definitions(g915_pico2w_bridge PRIVATE CYW43_LWIP=0)

set_source_files_properties(${BRIDGE_SOURCES} ${G915_BRIDGE_COMMON_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic"
)

# TinyUSB 0.18 declares a 32-bit enum value that GCC diagnoses under
# -Wpedantic. Keep the useful warnings on these translation units without
# surfacing a third-party header diagnostic we cannot fix here.
set_property(SOURCE
    main.c
    usb_descriptors.c
    ${G915_BRIDGE_COMMON_TUSB_SOURCES}
    APPEND PROPERTY COMPILE_OPTIONS "-Wno-pedantic"
)

target_include_directories(g915_pico2w_bridge PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(g915_pico2w_bridge PRIVATE
    g915_bridge_common
    pico_stdlib
    pico_unique_id
    pico_cyw43_arch_poll
    pico_btstack_cyw43
    pico_btstack_ble
    tinyusb_device
)

# USB stays a single HID keyboard. Optional diagnostics use UART0 on GP0/GP1.
pico_enable_stdio_usb(g915_pico2w_bridge 0)
pico_enable_stdio_uart(g915_pico2w_bridge 1)

pico_set_program_name(g915_pico2w_bridge "G915 Bridge")
pico_set_program_version(g915_pico2w_bridge "${PROJECT_VERSION}")
pico_add_extra_outputs(g915_pico2w_bridge)
