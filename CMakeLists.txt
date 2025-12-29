cmake_minimum_required(VERSION 3.14)

# =============================================================================
# Version extraction from header
# =============================================================================
file(READ "include/zxc_constants.h" version_header)
string(REGEX MATCH "#define ZXC_VERSION_MAJOR ([0-9]+)" _ "${version_header}")
set(MAJOR_VER ${CMAKE_MATCH_1})
string(REGEX MATCH "#define ZXC_VERSION_MINOR ([0-9]+)" _ "${version_header}")
set(MINOR_VER ${CMAKE_MATCH_1})
string(REGEX MATCH "#define ZXC_VERSION_PATCH ([0-9]+)" _ "${version_header}")
set(PATCH_VER ${CMAKE_MATCH_1})

project(zxc
    VERSION ${MAJOR_VER}.${MINOR_VER}.${PATCH_VER}
    LANGUAGES C
    DESCRIPTION "High-performance asymmetric lossless compression library"
)

# =============================================================================
# Build Options
# =============================================================================
option(ZXC_NATIVE_ARCH "Enable -march=native for maximum performance" ON)
option(ZXC_BUILD_CLI "Build the command-line interface" ON)
option(ZXC_BUILD_TESTS "Build unit tests" ON)

# =============================================================================
# C Standard
# =============================================================================
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# =============================================================================
# Core Library
# =============================================================================
add_library(zxc_lib STATIC
    src/lib/zxc_common.c
    src/lib/zxc_compress.c
    src/lib/zxc_decompress.c
    src/lib/zxc_driver.c
)

# Target-based include directories
target_include_directories(zxc_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
)

# =============================================================================
# Compiler-specific options (using generator expressions)
# =============================================================================
target_compile_options(zxc_lib PRIVATE
    # MSVC
    $<$<C_COMPILER_ID:MSVC>:/O2 /W3>
    # GCC/Clang common
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-O3 -Wall -Wextra -fomit-frame-pointer -fstrict-aliasing -ffast-math -flto>
    # Native architecture (optional)
    $<$<AND:$<NOT:$<C_COMPILER_ID:MSVC>>,$<BOOL:${ZXC_NATIVE_ARCH}>>:-march=native>
)

target_compile_definitions(zxc_lib PRIVATE
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:_GNU_SOURCE>
)

# Threading support
find_package(Threads REQUIRED)
target_link_libraries(zxc_lib PRIVATE Threads::Threads)

# Link-time optimization
if(NOT MSVC)
    target_link_options(zxc_lib PRIVATE -flto)
endif()

# =============================================================================
# CLI Executable
# =============================================================================
if(ZXC_BUILD_CLI)
    add_executable(zxc src/cli/main.c)
    target_link_libraries(zxc PRIVATE zxc_lib)
    
    # Math library on Unix
    if(UNIX)
        target_link_libraries(zxc PRIVATE m)
    endif()
    
    # Propagate compile options and definitions
    target_compile_options(zxc PRIVATE
        $<$<AND:$<NOT:$<C_COMPILER_ID:MSVC>>,$<BOOL:${ZXC_NATIVE_ARCH}>>:-march=native>
    )
    target_compile_definitions(zxc PRIVATE
        $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:_GNU_SOURCE>
    )
endif()

# =============================================================================
# Tests
# =============================================================================
if(ZXC_BUILD_TESTS)
    enable_testing()
    
    add_executable(zxc_test tests/test.c)
    target_link_libraries(zxc_test PRIVATE zxc_lib)
    
    # Propagate compile options
    target_compile_options(zxc_test PRIVATE
        $<$<AND:$<NOT:$<C_COMPILER_ID:MSVC>>,$<BOOL:${ZXC_NATIVE_ARCH}>>:-march=native>
    )
    
    add_test(NAME UnitTests COMMAND zxc_test)
endif()

# =============================================================================
# Installation
# =============================================================================
include(GNUInstallDirs)

install(TARGETS zxc_lib
    EXPORT zxc-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

if(ZXC_BUILD_CLI)
    install(TARGETS zxc
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# =============================================================================
# Summary
# =============================================================================
message(STATUS "")
message(STATUS "ZXC Configuration Summary:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Native Arch:    ${ZXC_NATIVE_ARCH}")
message(STATUS "  Build CLI:      ${ZXC_BUILD_CLI}")
message(STATUS "  Build Tests:    ${ZXC_BUILD_TESTS}")
message(STATUS "")
