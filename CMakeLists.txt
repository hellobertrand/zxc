# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause

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
option(BUILD_SHARED_LIBS "Build shared libraries instead of static" OFF)
option(ZXC_NATIVE_ARCH "Enable -march=native for maximum performance" ON)
option(ZXC_ENABLE_LTO "Enable Interprocedural Optimization (LTO)" ON)
set(ZXC_PGO_MODE "OFF" CACHE STRING "Profile-Guided Optimization mode (OFF/GENERATE/USE)")
set_property(CACHE ZXC_PGO_MODE PROPERTY STRINGS OFF GENERATE USE)
option(ZXC_BUILD_CLI "Build the command-line interface" ON)
option(ZXC_BUILD_TESTS "Build unit tests" ON)
option(ZXC_ENABLE_COVERAGE "Enable code coverage generation" OFF)
option(ZXC_DISABLE_SIMD "Disable explicit SIMD intrinsics (no AVX/NEON code paths)" OFF)

# =============================================================================
# Emscripten / WebAssembly overrides
# =============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    message(STATUS "Emscripten detected — configuring for WebAssembly.")
    set(ZXC_DISABLE_SIMD ON  CACHE BOOL "" FORCE)
    set(ZXC_BUILD_CLI    OFF CACHE BOOL "" FORCE)
    set(ZXC_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
    set(ZXC_NATIVE_ARCH  OFF CACHE BOOL "" FORCE)
    set(ZXC_ENABLE_LTO   OFF CACHE BOOL "" FORCE)
    set(ZXC_PGO_MODE     "OFF" CACHE STRING "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

if(ZXC_DISABLE_SIMD)
    add_compile_definitions(ZXC_DISABLE_SIMD)
endif()

# =============================================================================
# C Standard
# =============================================================================
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Enable _GNU_SOURCE for ftello/fseeko on Linux
# Enable 64-bit off_t on 32-bit Linux to prevent fseeko/ftello/pread truncation
if(UNIX AND NOT APPLE)
    add_compile_definitions(_GNU_SOURCE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
elseif(APPLE)
    add_compile_definitions(_GNU_SOURCE)
endif()

# Check for LTO support
if(ZXC_ENABLE_LTO AND NOT ZXC_ENABLE_COVERAGE)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT result OUTPUT output)
    if(result)
        message(STATUS "LTO/IPO is supported and enabled.")
    else()
        message(WARNING "LTO/IPO is not supported: ${output}")
        set(ZXC_ENABLE_LTO OFF)
    endif()
elseif(ZXC_ENABLE_COVERAGE)
    message(STATUS "Code coverage enabled: Disabling LTO and PGO.")
    set(ZXC_ENABLE_LTO OFF)
    set(ZXC_PGO_MODE "OFF")
endif()

# =============================================================================
# Rapidhash: system-installed (e.g. vcpkg) or vendored fallback
# =============================================================================
find_path(RAPIDHASH_INCLUDE_DIR rapidhash.h)
if(NOT RAPIDHASH_INCLUDE_DIR)
    set(RAPIDHASH_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/vendors")
    message(STATUS "Using vendored rapidhash from ${RAPIDHASH_INCLUDE_DIR}")
else()
    message(STATUS "Found system rapidhash in ${RAPIDHASH_INCLUDE_DIR}")
endif()

# =============================================================================
# Core Library & Runtime Dispatch
# =============================================================================

# --- PGO flag selection (Clang vs GCC) ---
set(ZXC_PGO_DIR "${CMAKE_BINARY_DIR}/pgo")
set(ZXC_PGO_GEN_CFLAGS "")
set(ZXC_PGO_GEN_LDFLAGS "")
set(ZXC_PGO_USE_CFLAGS "")
set(ZXC_PGO_USE_LDFLAGS "")

if(NOT MSVC AND NOT ZXC_PGO_MODE STREQUAL "OFF")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Clang: instrumentation-based PGO
        set(ZXC_PGO_PROFDATA "${ZXC_PGO_DIR}/default.profdata")
        set(ZXC_PGO_GEN_CFLAGS  -fprofile-instr-generate=${ZXC_PGO_DIR}/default_%m.profraw)
        set(ZXC_PGO_GEN_LDFLAGS -fprofile-instr-generate)
        set(ZXC_PGO_USE_CFLAGS  -fprofile-instr-use=${ZXC_PGO_PROFDATA})
        set(ZXC_PGO_USE_LDFLAGS -fprofile-instr-use=${ZXC_PGO_PROFDATA})
    else()
        # GCC: directory-based PGO
        set(ZXC_PGO_GEN_CFLAGS  -fprofile-generate=${ZXC_PGO_DIR})
        set(ZXC_PGO_GEN_LDFLAGS -fprofile-generate=${ZXC_PGO_DIR})
        set(ZXC_PGO_USE_CFLAGS  -fprofile-use=${ZXC_PGO_DIR} -fprofile-correction)
        set(ZXC_PGO_USE_LDFLAGS -fprofile-use=${ZXC_PGO_DIR})
    endif()
endif()

# Helper: apply PGO flags to a target
macro(zxc_apply_pgo target)
    if(ZXC_PGO_MODE STREQUAL "GENERATE")
        target_compile_options(${target} PRIVATE ${ZXC_PGO_GEN_CFLAGS})
        target_link_options(${target} PRIVATE ${ZXC_PGO_GEN_LDFLAGS})
    elseif(ZXC_PGO_MODE STREQUAL "USE")
        if(EXISTS "${ZXC_PGO_DIR}")
            target_compile_options(${target} PRIVATE ${ZXC_PGO_USE_CFLAGS})
            target_link_options(${target} PRIVATE ${ZXC_PGO_USE_LDFLAGS})
        endif()
    endif()
endmacro()

# Function Multi-Versioning Helper
# Compiles src/lib/zxc_compress.c and src/lib/zxc_decompress.c with specific flags and suffix.
macro(zxc_add_variant suffix flags)
    add_library(zxc_compress${suffix} OBJECT src/lib/zxc_compress.c)
    target_compile_options(zxc_compress${suffix} PRIVATE ${flags})
    target_compile_definitions(zxc_compress${suffix} PRIVATE ZXC_FUNCTION_SUFFIX=${suffix})
    # For static builds, define ZXC_STATIC_DEFINE
    if(NOT BUILD_SHARED_LIBS)
        target_compile_definitions(zxc_compress${suffix} PRIVATE ZXC_STATIC_DEFINE)
    else()
        # Mark as part of the DLL being built (avoids dllimport on internal symbols)
        target_compile_definitions(zxc_compress${suffix} PRIVATE zxc_lib_EXPORTS)
        set_target_properties(zxc_compress${suffix} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        # Hide variant symbols from shared library public ABI
        if(NOT MSVC)
            target_compile_options(zxc_compress${suffix} PRIVATE -fvisibility=hidden)
        endif()
    endif()
    # Inherit include directories
    target_include_directories(zxc_compress${suffix} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/lib ${RAPIDHASH_INCLUDE_DIR} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

    add_library(zxc_decompress${suffix} OBJECT src/lib/zxc_decompress.c)
    target_compile_options(zxc_decompress${suffix} PRIVATE ${flags})
    target_compile_definitions(zxc_decompress${suffix} PRIVATE ZXC_FUNCTION_SUFFIX=${suffix})
    # For static builds, define ZXC_STATIC_DEFINE
    if(NOT BUILD_SHARED_LIBS)
        target_compile_definitions(zxc_decompress${suffix} PRIVATE ZXC_STATIC_DEFINE)
    else()
        # Mark as part of the DLL being built (avoids dllimport on internal symbols)
        target_compile_definitions(zxc_decompress${suffix} PRIVATE zxc_lib_EXPORTS)
        set_target_properties(zxc_decompress${suffix} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        # Hide variant symbols from shared library public ABI
        if(NOT MSVC)
            target_compile_options(zxc_decompress${suffix} PRIVATE -fvisibility=hidden)
        endif()
    endif()
    target_include_directories(zxc_decompress${suffix} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/lib ${RAPIDHASH_INCLUDE_DIR} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

    # Propagate PGO flags to variant objects
    zxc_apply_pgo(zxc_compress${suffix})
    zxc_apply_pgo(zxc_decompress${suffix})

    list(APPEND ZXC_VARIANT_OBJECTS $<TARGET_OBJECTS:zxc_compress${suffix}> $<TARGET_OBJECTS:zxc_decompress${suffix}>)
endmacro()

set(ZXC_VARIANT_OBJECTS "")

# --- 1. Default Variant (Scalar/Baseline) ---
zxc_add_variant(_default "")

# --- 2. Architecture Specific Variants (skipped in no-intrinsics mode) ---
if(ZXC_DISABLE_SIMD)
    message(STATUS "ZXC_DISABLE_SIMD: Skipping SIMD variants (no explicit AVX/NEON code paths).")
else()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "amd64|x86_64|AMD64")
    message(STATUS "Building x86_64 AVX2 and AVX512 variants...")
    if(MSVC)
        # AVX2 for MSVC (Enables AVX2/BMI1/BMI2 sets)
        zxc_add_variant(_avx2 "/arch:AVX2;/D__BMI__;/D__BMI2__;/D__LZCNT__")
        # AVX512 for MSVC (VS2019 16.10+ supports /arch:AVX512)
        zxc_add_variant(_avx512 "/arch:AVX512;/D__BMI__;/D__BMI2__;/D__LZCNT__")
    else()
        # AVX2 for GCC/Clang
        zxc_add_variant(_avx2 "-mavx2;-mfma;-mbmi;-mbmi2;-mlzcnt")
        # AVX512 for GCC/Clang
        zxc_add_variant(_avx512 "-mavx512f;-mavx512bw;-mbmi;-mbmi2;-mlzcnt")
    endif()

elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    message(STATUS "Building AArch64 NEON variant...")
    if(MSVC)
        # MSVC for ARM64 implies NEON support by default.
        zxc_add_variant(_neon "")
    else()
        # NEON is usually default on AArch64, but we add a specific variant for structure
        zxc_add_variant(_neon "-march=armv8-a+simd")
    endif()

elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
    message(STATUS "Building ARM NEON variant...")
    zxc_add_variant(_neon "-march=armv7-a;-mfpu=neon")
endif()
endif()

# Sources: exclude zxc_driver.c for Emscripten (requires pthreads + FILE* I/O)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(ZXC_CORE_SOURCES
        src/lib/zxc_common.c
        src/lib/zxc_dispatch.c
        src/lib/zxc_seekable.c
        src/lib/zxc_suffix_array.c
    )
else()
    set(ZXC_CORE_SOURCES
        src/lib/zxc_common.c
        src/lib/zxc_driver.c
        src/lib/zxc_dispatch.c
        src/lib/zxc_seekable.c
        src/lib/zxc_suffix_array.c
    )
endif()

add_library(zxc_lib
    ${ZXC_CORE_SOURCES}
    ${ZXC_VARIANT_OBJECTS}
)

# =============================================================================
# ABI Versioning (Debian/Linux shared libraries)
# =============================================================================
# Increment this number ONLY when breaking the ABI. 
set(ZXC_SOVERSION 3)

# Set library output name and version
set_target_properties(zxc_lib PROPERTIES
    OUTPUT_NAME zxc
    VERSION ${PROJECT_VERSION}
    SOVERSION ${ZXC_SOVERSION}
)

# Target-based include directories for the main lib
target_include_directories(zxc_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
        ${RAPIDHASH_INCLUDE_DIR}
)

# Symbol visibility for shared libraries
if(BUILD_SHARED_LIBS)
    # Set visibility for GCC/Clang
    if(NOT MSVC)
        target_compile_options(zxc_lib PRIVATE -fvisibility=hidden)
        set_target_properties(zxc_lib PROPERTIES
            C_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
        )
    endif()
else()
    # For static libraries, define ZXC_STATIC_DEFINE to avoid dllimport/dllexport
    target_compile_definitions(zxc_lib PUBLIC ZXC_STATIC_DEFINE)
endif()

# =============================================================================
# Compiler-specific options (using generator expressions)
# =============================================================================
if(NOT MSVC)
    target_compile_options(zxc_lib PRIVATE
        $<$<NOT:$<BOOL:${ZXC_ENABLE_COVERAGE}>>:-O3>
        -Wall -Wextra 
        -fomit-frame-pointer 
        -fstrict-aliasing
        # Native Arch
        $<$<BOOL:${ZXC_NATIVE_ARCH}>:-march=native>
        # Dead code elimination
        -ffunction-sections -fdata-sections
    )
    
    # Profile-Guided Optimization
    zxc_apply_pgo(zxc_lib)
    if(ZXC_PGO_MODE STREQUAL "GENERATE")
        message(STATUS "PGO: Generating profile data to ${ZXC_PGO_DIR}")
    elseif(ZXC_PGO_MODE STREQUAL "USE")
        if(NOT EXISTS "${ZXC_PGO_DIR}")
            message(FATAL_ERROR "PGO: Profile data not found at ${ZXC_PGO_DIR}. Run with ZXC_PGO_MODE=GENERATE first.")
        endif()
        message(STATUS "PGO: Using profile data from ${ZXC_PGO_DIR}")
    endif()

    # Linker options for Dead Code Stripping
    if(APPLE)
        target_link_options(zxc_lib PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(zxc_lib PRIVATE -Wl,--gc-sections)
    endif()
else()
    target_compile_options(zxc_lib PRIVATE $<$<CONFIG:Release>:/O2> /W3)
    target_compile_definitions(zxc_lib PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_compile_definitions(zxc_lib PRIVATE
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:_GNU_SOURCE>
)

# Coverage flags
if(ZXC_ENABLE_COVERAGE)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(zxc_lib PRIVATE --coverage -fprofile-update=atomic)
        target_link_options(zxc_lib PRIVATE --coverage)
    endif()
endif()

# Enable LTO cleanly
if(ZXC_ENABLE_LTO)
    set_property(TARGET zxc_lib PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    if(NOT MSVC)
        target_compile_options(zxc_lib PRIVATE -flto)
        target_link_options(zxc_lib PRIVATE -flto)
    endif()
endif()

# Threading support (not available in WASM single-threaded builds)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    find_package(Threads REQUIRED)
    target_link_libraries(zxc_lib PRIVATE Threads::Threads)
endif()

# =============================================================================
# CLI Executable
# =============================================================================
if(ZXC_BUILD_CLI)
    add_executable(zxc src/cli/main.c)
    target_link_libraries(zxc PRIVATE zxc_lib)
    target_include_directories(zxc PRIVATE ${RAPIDHASH_INCLUDE_DIR})
    
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

    # Coverage flags for CLI
    if(ZXC_ENABLE_COVERAGE)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(zxc PRIVATE --coverage)
            target_link_options(zxc PRIVATE --coverage)
        endif()
    endif()

    # Enable LTO cleanly
    if(ZXC_ENABLE_LTO)
        set_property(TARGET zxc PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        if(NOT MSVC)
            target_compile_options(zxc PRIVATE -flto)
            target_link_options(zxc PRIVATE -flto)
        endif()
    endif()
    
    # Profile-Guided Optimization for CLI
    zxc_apply_pgo(zxc)

    # Linker options for Dead Code Stripping
    if(NOT MSVC)
        if(APPLE)
            target_link_options(zxc PRIVATE -Wl,-dead_strip)
        else()
            target_link_options(zxc PRIVATE -Wl,--gc-sections)
        endif()
    endif()
    
    # Strip symbols in Release mode for smaller binary
    if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
        # Set default strip command if not already set (e.g., for cross-compilation)
        if(NOT CMAKE_STRIP)
            set(CMAKE_STRIP strip)
        endif()
        
        add_custom_command(TARGET zxc POST_BUILD
            COMMAND ${CMAKE_STRIP} $<TARGET_FILE:zxc>
            COMMENT "Stripping symbols from zxc"
        )
    endif()
endif()

# =============================================================================
# Tests
# =============================================================================
if(ZXC_BUILD_TESTS)
    enable_testing()
    
    add_executable(zxc_test
        tests/test_main.c
        tests/test_common.c
        tests/test_buffer_api.c
        tests/test_block_api.c
        tests/test_context_api.c
        tests/test_stream_api.c
        tests/test_seekable.c
        tests/test_seekable_mt.c
        tests/test_format.c
        tests/test_misc.c
    )
    
    # When building shared libraries, create a static version for tests
    # This allows tests to access internal functions for unit testing
    if(BUILD_SHARED_LIBS)
        # Create a static library specifically for tests
        add_library(zxc_lib_static STATIC
            src/lib/zxc_common.c
            src/lib/zxc_driver.c
            src/lib/zxc_dispatch.c
            src/lib/zxc_seekable.c
            ${ZXC_VARIANT_OBJECTS}
        )
        
        # Copy all properties from the shared library
        target_include_directories(zxc_lib_static
            PUBLIC
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include>
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
                ${RAPIDHASH_INCLUDE_DIR}
        )
        
        # Apply same compiler settings as main library
        target_compile_options(zxc_lib_static PRIVATE
            $<TARGET_PROPERTY:zxc_lib,COMPILE_OPTIONS>
        )
        
        target_compile_definitions(zxc_lib_static PUBLIC ZXC_STATIC_DEFINE)
        target_link_libraries(zxc_lib_static PRIVATE Threads::Threads)
        
        # Link tests against static library
        target_link_libraries(zxc_test PRIVATE zxc_lib_static)
    else()
        # For static builds, use the main library
        target_link_libraries(zxc_test PRIVATE zxc_lib)
    endif()
    
    # Propagate compile options
    target_compile_options(zxc_test PRIVATE
        $<$<AND:$<NOT:$<C_COMPILER_ID:MSVC>>,$<BOOL:${ZXC_NATIVE_ARCH}>>:-march=native>
    )
    
    # Coverage flags for Tests
    if(ZXC_ENABLE_COVERAGE)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_link_options(zxc_test PRIVATE --coverage)
        endif()
    endif()
    
    # Profile-Guided Optimization for tests
    zxc_apply_pgo(zxc_test)
    
    target_include_directories(zxc_test PRIVATE src/lib ${RAPIDHASH_INCLUDE_DIR})

    file(STRINGS tests/test_main.c ZXC_TEST_CASE_LINES REGEX "TEST_CASE\\(")
    set(ZXC_TEST_NAMES "")
    foreach(_line IN LISTS ZXC_TEST_CASE_LINES)
        if(_line MATCHES "TEST_CASE\\(([A-Za-z0-9_]+)\\)")
            list(APPEND ZXC_TEST_NAMES ${CMAKE_MATCH_1})
        endif()
    endforeach()
    foreach(_name IN LISTS ZXC_TEST_NAMES)
        add_test(NAME ${_name} COMMAND zxc_test --exact ${_name})
    endforeach()
endif()

# =============================================================================
# Installation
# =============================================================================
include(GNUInstallDirs)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/libzxc.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/libzxc.pc
    @ONLY
)

install(TARGETS zxc_lib
    EXPORT zxc-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
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

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libzxc.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# CMake package config files for find_package(zxc)
include(CMakePackageConfigHelpers)

install(EXPORT zxc-targets
    FILE zxc-targets.cmake
    NAMESPACE zxc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zxc
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/zxcConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/zxcConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zxc
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/zxcConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/zxcConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/zxcConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zxc
)

# =============================================================================
# Code Formatting (clang-format)
# =============================================================================
# Allow override via environment variable (e.g., CLANG_FORMAT=clang-format-22)
if(DEFINED ENV{CLANG_FORMAT})
    set(CLANG_FORMAT "$ENV{CLANG_FORMAT}")
else()
    find_program(CLANG_FORMAT clang-format)
endif()
if(CLANG_FORMAT)
    file(GLOB_RECURSE ZXC_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/*.h"
    )
    # Exclude vendored third-party code
    list(FILTER ZXC_FORMAT_SOURCES EXCLUDE REGEX ".*/vendors/.*")

    add_custom_target(format
        COMMAND ${CLANG_FORMAT} --style=file -i ${ZXC_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting ${CMAKE_CURRENT_SOURCE_DIR}/include and src/lib with clang-format"
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --style=file --dry-run --Werror ${ZXC_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking formatting of include/ and src/lib/"
        VERBATIM
    )
else()
    message(STATUS "clang-format not found - format/format-check targets disabled")
endif()

# =============================================================================
# Summary
# =============================================================================
message(STATUS "")
message(STATUS "ZXC Configuration Summary:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
if(BUILD_SHARED_LIBS)
    message(STATUS "  Library Type:   Shared")
else()
    message(STATUS "  Library Type:   Static")
endif()
message(STATUS "  Native Arch:    ${ZXC_NATIVE_ARCH}")
message(STATUS "  Disable SIMD:   ${ZXC_DISABLE_SIMD}")
message(STATUS "  LTO Enabled:    ${ZXC_ENABLE_LTO}")
message(STATUS "  PGO Mode:       ${ZXC_PGO_MODE}")
message(STATUS "  Build CLI:      ${ZXC_BUILD_CLI}")
message(STATUS "  Build Tests:    ${ZXC_BUILD_TESTS}")
message(STATUS "")

# =============================================================================
# WebAssembly (Emscripten) Target
# =============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Exported C functions (with leading underscore for Emscripten convention)
    set(ZXC_WASM_EXPORTS
        "_zxc_compress"
        "_zxc_decompress"
        "_zxc_compress_bound"
        "_zxc_compress_block_bound"
        "_zxc_get_decompressed_size"
        "_zxc_create_cctx"
        "_zxc_free_cctx"
        "_zxc_compress_cctx"
        "_zxc_create_dctx"
        "_zxc_free_dctx"
        "_zxc_decompress_dctx"
        "_zxc_min_level"
        "_zxc_max_level"
        "_zxc_default_level"
        "_zxc_version_string"
        "_zxc_error_name"
        "_malloc"
        "_free"
    )
    # Join list with commas for Emscripten linker flag
    string(JOIN "," ZXC_WASM_EXPORTS_STR ${ZXC_WASM_EXPORTS})

    add_executable(zxc_wasm wrappers/wasm/wasm_entry.c)
    target_link_libraries(zxc_wasm PRIVATE zxc_lib)
    set_target_properties(zxc_wasm PROPERTIES
        OUTPUT_NAME "zxc"
        SUFFIX ".js"
    )
    target_link_options(zxc_wasm PRIVATE
        "-sEXPORTED_FUNCTIONS=[${ZXC_WASM_EXPORTS_STR}]"
        "-sEXPORTED_RUNTIME_METHODS=[ccall,cwrap,getValue,setValue,UTF8ToString,HEAPU8,HEAP32,HEAPU32]"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=ZXCModule"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=2097152"
        "-sSTACK_SIZE=131072"
        "-sENVIRONMENT=web,node"
        "-sNO_FILESYSTEM=1"
        "-sSTRICT=1"
    )
    target_compile_options(zxc_wasm PRIVATE -O3)

    message(STATUS "  WASM Target:    zxc.js + zxc.wasm")
endif()

# =============================================================================
# Documentation (Doxygen)
# =============================================================================
find_package(Doxygen)
if(DOXYGEN_FOUND)
    # Generate the Doxyfile with the current project version
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
    
    # Add a 'doc' target to generate documentation (e.g. 'make doc' or 'cmake --build . --target doc')
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()
