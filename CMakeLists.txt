cmake_minimum_required(VERSION 3.10)

# Versioning
file(READ "include/zxc_constants.h" version_header)
string(REGEX MATCH "#define ZXC_VERSION_MAJOR ([0-9]+)" _ "${version_header}")
set(MAJOR_VER ${CMAKE_MATCH_1})
string(REGEX MATCH "#define ZXC_VERSION_MINOR ([0-9]+)" _ "${version_header}")
set(MINOR_VER ${CMAKE_MATCH_1})
string(REGEX MATCH "#define ZXC_VERSION_PATCH ([0-9]+)" _ "${version_header}")
set(PATCH_VER ${CMAKE_MATCH_1})
project(zxc_project VERSION ${MAJOR_VER}.${MINOR_VER}.${PATCH_VER} LANGUAGES C)

# Standards
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compilation options configuration
if(MSVC)
    add_compile_options(/O2 /W3 /arch:AVX2)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-O3 -march=native -flto -fomit-frame-pointer -fstrict-aliasing -ffast-math -pthread -Wall -Wextra -D_GNU_SOURCE)
endif()

# Headers paths
include_directories(include src/lib)

# Core
add_library(zxc_lib STATIC
    src/lib/zxc_common.c
    src/lib/zxc_compress.c
    src/lib/zxc_decompress.c
    src/lib/zxc_driver.c
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
