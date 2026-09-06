cmake_minimum_required(VERSION 3.16)

project(g915_pico2w_bridge_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

# The platform-independent core tests live with the shared code and are pulled
# in here so one configure of this directory runs everything the Pico 2 W
# bridge depends on.
add_subdirectory(../../common/tests common_tests)

# Decoder tests use BTstack's real HID parser and are added only when a Pico
# SDK checkout is available to this standalone test project.
set(BRIDGE_PICO_SDK_PATH "")
if(DEFINED PICO_SDK_PATH AND NOT "${PICO_SDK_PATH}" STREQUAL "")
    set(BRIDGE_PICO_SDK_PATH "${PICO_SDK_PATH}")
elseif(DEFINED ENV{PICO_SDK_PATH} AND NOT "$ENV{PICO_SDK_PATH}" STREQUAL "")
    set(BRIDGE_PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()

set(BTSTACK_SOURCE_DIR
    "${BRIDGE_PICO_SDK_PATH}/lib/btstack/src"
)
if(BRIDGE_PICO_SDK_PATH AND
   EXISTS "${BTSTACK_SOURCE_DIR}/btstack_hid_parser.c")
    add_executable(g915_bridge_decoder_tests
        test_hid_keyboard_decoder.c
        ../ble/hid_keyboard_decoder.c
        ../../common/core/keyboard_state.c
        "${BTSTACK_SOURCE_DIR}/btstack_hid_parser.c"
    )
    target_include_directories(g915_bridge_decoder_tests PRIVATE
        support
        ..
        ../../common
        "${BTSTACK_SOURCE_DIR}"
    )

    if(MSVC)
        target_compile_options(g915_bridge_decoder_tests PRIVATE
            /W4 /WX /UNDEBUG
        )
    else()
        target_compile_options(g915_bridge_decoder_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror -UNDEBUG
        )
    endif()

    add_test(NAME g915_bridge_decoder_tests
        COMMAND g915_bridge_decoder_tests
    )
elseif(BRIDGE_PICO_SDK_PATH)
    message(WARNING
        "PICO_SDK_PATH does not contain BTstack's HID parser; "
        "decoder tests are disabled"
    )
else()
    message(STATUS
        "PICO_SDK_PATH is unavailable; decoder tests are disabled"
    )
endif()
