cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(g915_pico2w_bridge C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(g915_pico2w_bridge
    main.c
    hog_boot_host.c
    usb_descriptors.c
)

target_include_directories(g915_pico2w_bridge PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(g915_pico2w_bridge PRIVATE
    pico_stdlib
    pico_unique_id
    pico_cyw43_arch_none
    pico_btstack_cyw43
    pico_btstack_ble
    tinyusb_device
)

# USB stays a single HID keyboard. Optional diagnostics use UART0 on GP0/GP1.
pico_enable_stdio_usb(g915_pico2w_bridge 0)
pico_enable_stdio_uart(g915_pico2w_bridge 1)

pico_set_program_name(g915_pico2w_bridge "G915 Bridge")
pico_set_program_version(g915_pico2w_bridge "0.1.0")
pico_add_extra_outputs(g915_pico2w_bridge)

