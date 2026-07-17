cmake_minimum_required(VERSION 3.21)
project(cuif C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "cuif currently only supports Windows (Win32 + WGL backend). See Plugins/Reverb/ARCHITECTURE_DECISIONS.md.")
endif()

add_library(cuif STATIC
    src/window_win32.c
    src/graphics.c
    src/font.c
    src/widget.c
    src/theme.c
)

target_include_directories(cuif PUBLIC include)
target_link_libraries(cuif PUBLIC opengl32 gdi32 user32)
target_compile_definitions(cuif PUBLIC UNICODE _UNICODE)

option(CUIF_BUILD_HARNESS "Build the standalone cuif test harness app" ON)
if(CUIF_BUILD_HARNESS)
    add_executable(cuif_harness WIN32 test/harness.c)
    target_link_libraries(cuif_harness PRIVATE cuif)
endif()

option(CUIF_BUILD_TESTS "Build the cuif unit test executable" ON)
if(CUIF_BUILD_TESTS)
    add_executable(cuif_tests test/cuif_tests.c)
    target_link_libraries(cuif_tests PRIVATE cuif)
endif()
