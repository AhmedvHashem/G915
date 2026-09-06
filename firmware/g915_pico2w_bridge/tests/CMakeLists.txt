cmake_minimum_required(VERSION 3.16)

project(g915_bridge_core_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

add_executable(g915_bridge_core_tests
    test_core.c
    ../core/keyboard_state.c
    ../core/report_pipe.c
)
target_include_directories(g915_bridge_core_tests PRIVATE
    ..
    ../core
)

if(MSVC)
    target_compile_options(g915_bridge_core_tests PRIVATE /W4 /WX /UNDEBUG)
else()
    target_compile_options(g915_bridge_core_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG
    )
endif()

add_test(NAME g915_bridge_core_tests COMMAND g915_bridge_core_tests)

# The core tests above intentionally have no SDK dependency. Decoder tests use
# BTstack's real HID parser and are added only when a Pico SDK checkout is
# available to this standalone test project.
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
        ../core/keyboard_state.c
        "${BTSTACK_SOURCE_DIR}/btstack_hid_parser.c"
    )
    target_include_directories(g915_bridge_decoder_tests PRIVATE
        support
        ..
        ../core
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
