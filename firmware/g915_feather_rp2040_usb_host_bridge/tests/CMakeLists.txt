cmake_minimum_required(VERSION 3.16)

project(g915_feather_bridge_tests C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

enable_testing()

# The shared keyboard-state and report-pipe tests live with the shared code.
add_subdirectory(../../common/tests common_tests)

# Frame phase alignment is pure arithmetic over 32-bit timestamps and needs no
# Pico SDK, so it is tested on the development machine.
add_executable(g915_feather_frame_phase_tests
    test_frame_phase.c
    ../host/frame_phase.c
)
target_include_directories(g915_feather_frame_phase_tests PRIVATE
    ..
)

if(MSVC)
    target_compile_options(g915_feather_frame_phase_tests PRIVATE
        /W4 /WX /UNDEBUG
    )
else()
    target_compile_options(g915_feather_frame_phase_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror -UNDEBUG
    )
endif()

add_test(NAME g915_feather_frame_phase_tests
    COMMAND g915_feather_frame_phase_tests
)
