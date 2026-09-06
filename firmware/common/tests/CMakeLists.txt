cmake_minimum_required(VERSION 3.16)

project(g915_bridge_core_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

# Platform-independent tests for the keyboard state encoder and the report
# pipe. They build with any host C compiler and need no Pico SDK.
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
