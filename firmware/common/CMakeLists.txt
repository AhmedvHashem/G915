# Shared bridge code used by every G915 bridge firmware.
#
#   core/  platform-independent keyboard state and the ordered report pipe
#   usb/   the TinyUSB device-side boot-keyboard adapter
#
# This is an INTERFACE library on purpose. The sources compile inside each
# consuming firmware target, so they pick up that target's app_config.h and
# tusb_config.h. Consumers receive two variables for per-source warning
# options, because target-wide warnings would also apply to the TinyUSB
# sources the Pico SDK adds to the same target:
#
#   G915_BRIDGE_COMMON_SOURCES       every shared source file
#   G915_BRIDGE_COMMON_TUSB_SOURCES  the subset that includes tusb.h
cmake_minimum_required(VERSION 3.13)

set(G915_BRIDGE_COMMON_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/core/keyboard_state.c
    ${CMAKE_CURRENT_LIST_DIR}/core/report_pipe.c
    ${CMAKE_CURRENT_LIST_DIR}/usb/usb_keyboard.c
)
set(G915_BRIDGE_COMMON_TUSB_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/usb/usb_keyboard.c
)

if(NOT TARGET g915_bridge_common)
    add_library(g915_bridge_common INTERFACE)
    target_sources(g915_bridge_common INTERFACE ${G915_BRIDGE_COMMON_SOURCES})
    target_include_directories(g915_bridge_common INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )
    target_link_libraries(g915_bridge_common INTERFACE
        pico_stdlib
        tinyusb_device
    )
endif()

set(G915_BRIDGE_COMMON_SOURCES "${G915_BRIDGE_COMMON_SOURCES}" PARENT_SCOPE)
set(G915_BRIDGE_COMMON_TUSB_SOURCES "${G915_BRIDGE_COMMON_TUSB_SOURCES}"
    PARENT_SCOPE)
