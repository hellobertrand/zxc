cmake_minimum_required(VERSION 3.10)
project(zxc_project VERSION 1.0 LANGUAGES C)

# Standards
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compilation options configuration
if(MSVC)
    add_compile_options(/O2 /W3 /arch:AVX2)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-O3 -march=native -flto -fomit-frame-pointer -fstrict-aliasing -ffast-math -flax-vector-conversions -pthread -Wall -Wextra -D_GNU_SOURCE)
endif()

# Headers paths
include_directories(include src/lib)

# Core
add_library(zxc_lib STATIC
    src/lib/zxc_common.c
    src/lib/zxc_compress.c
    src/lib/zxc_decompress.c
)

# Thread Management (Portable Abstraction)
find_package(Threads REQUIRED)
target_link_libraries(zxc_lib PRIVATE Threads::Threads)

# Executable (CLI)
add_executable(zxc src/cli/main.c)
if(WIN32)
    target_link_libraries(zxc zxc_lib)
else()
    target_link_libraries(zxc zxc_lib m)
endif()

# Installation (Required for CPack/CI packaging)
install(TARGETS zxc RUNTIME DESTINATION bin)

# Tests
enable_testing()

# Test executable
add_executable(zxc_test tests/test.c)

# Link with the static library
target_link_libraries(zxc_test zxc_lib)

# Register the test for CTest
add_test(NAME UnitTests COMMAND zxc_test)
