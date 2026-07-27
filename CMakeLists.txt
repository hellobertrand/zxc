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
if(NOT DEFINED PROJECT_IS_TOP_LEVEL)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(PROJECT_IS_TOP_LEVEL ON)
    else()
        set(PROJECT_IS_TOP_LEVEL OFF)
    endif()
endif()

option(BUILD_SHARED_LIBS "Build shared libraries instead of static" OFF)
option(ZXC_NATIVE_ARCH "Enable -march=native for maximum performance" ${PROJECT_IS_TOP_LEVEL})
option(ZXC_ENABLE_LTO "Enable Interprocedural Optimization (LTO)" ${PROJECT_IS_TOP_LEVEL})
set(ZXC_PGO_MODE "OFF" CACHE STRING "Profile-Guided Optimization mode (OFF/GENERATE/USE)")
set_property(CACHE ZXC_PGO_MODE PROPERTY STRINGS OFF GENERATE USE)
option(ZXC_BUILD_CLI "Build the command-line interface" ${PROJECT_IS_TOP_LEVEL})
option(ZXC_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(ZXC_INSTALL "Generate install rules (headers, pkg-config, CMake package)"
       ${PROJECT_IS_TOP_LEVEL})
option(ZXC_ENABLE_COVERAGE "Enable code coverage generation" OFF)
option(ZXC_DISABLE_SIMD "Disable explicit SIMD intrinsics (no AVX/NEON code paths)" OFF)
option(ZXC_USE_SYSTEM_RAPIDHASH "Use a system-installed rapidhash.h instead of the vendored copy"
       OFF)

# =============================================================================
# Target architecture
# =============================================================================
set(ZXC_TARGET_ARCHS "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(ZXC_TARGET_ARCHS "${CMAKE_OSX_ARCHITECTURES}")
endif()

set(ZXC_TARGET_X86 OFF)
set(ZXC_TARGET_AARCH64 OFF)
set(ZXC_TARGET_ARM32 OFF)
foreach(_arch IN LISTS ZXC_TARGET_ARCHS)
    if(_arch MATCHES "amd64|x86_64|AMD64")
        set(ZXC_TARGET_X86 ON)
    elseif(_arch MATCHES "aarch64|arm64|ARM64")
        # Must be tested before "^arm", which "arm64" matches too.
        set(ZXC_TARGET_AARCH64 ON)
    elseif(_arch MATCHES "^arm")
        set(ZXC_TARGET_ARM32 ON)
    endif()
endforeach()

if(ZXC_NATIVE_ARCH AND (CMAKE_CROSSCOMPILING OR
                        NOT "${ZXC_TARGET_ARCHS}" STREQUAL "${CMAKE_SYSTEM_PROCESSOR}"))
    message(STATUS "zxc: target (${ZXC_TARGET_ARCHS}) is not the build host "
                   "(${CMAKE_SYSTEM_PROCESSOR}) - ignoring ZXC_NATIVE_ARCH.")
    set(ZXC_NATIVE_ARCH OFF)
endif()

# =============================================================================
# Emscripten / WebAssembly overrides
# =============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    message(STATUS "Emscripten detected - configuring for WebAssembly.")
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
if(ZXC_USE_SYSTEM_RAPIDHASH)
    find_path(RAPIDHASH_INCLUDE_DIR rapidhash.h)
    if(NOT RAPIDHASH_INCLUDE_DIR)
        message(FATAL_ERROR "ZXC_USE_SYSTEM_RAPIDHASH is ON but rapidhash.h was not found.")
    endif()
    message(STATUS "Using system rapidhash from ${RAPIDHASH_INCLUDE_DIR}")
else()
    set(RAPIDHASH_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/vendors")
    message(STATUS "Using vendored rapidhash from ${RAPIDHASH_INCLUDE_DIR}")
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

# Warnings and PGO, for every zxc target. Defined once because each target used
# to repeat its own list, and the variant objects ended up with no warnings at
# all. Warning level is top-level only: an embedder sets its own.
macro(zxc_apply_common_flags target)
    if(MSVC)
        # /wd4244: block-bounded uint64->size_t narrowing, lossless.
        target_compile_options(${target} PRIVATE /wd4244)
        if(PROJECT_IS_TOP_LEVEL)
            target_compile_options(${target} PRIVATE /W3)
        endif()
    elseif(PROJECT_IS_TOP_LEVEL)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
    zxc_apply_pgo(${target})
endmacro()

# Function Multi-Versioning Helper
# Compiles compress/decompress/huffman with specific flags and suffix so the
# runtime dispatcher can route to a BMI2/AVX2/AVX512/NEON-aware Huffman codec
# in addition to the LZ77 stages.
macro(zxc_add_variant suffix flags)
    foreach(_src compress decompress huffman)
        add_library(zxc_${_src}${suffix} OBJECT src/lib/zxc_${_src}.c)
        # Common flags first: the ISA flags below must have the last word, since
        # a later -march= would reset the feature set they select.
        zxc_apply_common_flags(zxc_${_src}${suffix})
        target_compile_options(zxc_${_src}${suffix} PRIVATE ${flags})
        target_compile_definitions(zxc_${_src}${suffix} PRIVATE ZXC_FUNCTION_SUFFIX=${suffix})
        # For static builds, define ZXC_STATIC_DEFINE
        if(NOT BUILD_SHARED_LIBS)
            target_compile_definitions(zxc_${_src}${suffix} PRIVATE ZXC_STATIC_DEFINE)
        else()
            # Mark as part of the DLL being built (avoids dllimport on internal symbols)
            target_compile_definitions(zxc_${_src}${suffix} PRIVATE zxc_lib_EXPORTS)
            set_target_properties(zxc_${_src}${suffix} PROPERTIES POSITION_INDEPENDENT_CODE ON)
            # Hide variant symbols from shared library public ABI
            if(NOT MSVC)
                target_compile_options(zxc_${_src}${suffix} PRIVATE -fvisibility=hidden)
            endif()
        endif()
        # Inherit include directories
        target_include_directories(zxc_${_src}${suffix} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/lib ${RAPIDHASH_INCLUDE_DIR} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

        list(APPEND ZXC_VARIANT_OBJECTS $<TARGET_OBJECTS:zxc_${_src}${suffix}>)
    endforeach()
endmacro()

set(ZXC_VARIANT_OBJECTS "")

# --- 1. Default Variant (Scalar/Baseline) ---
zxc_add_variant(_default "")

# --- 2. Architecture Specific Variants (skipped in no-intrinsics mode) ---
if(ZXC_DISABLE_SIMD)
    message(STATUS "ZXC_DISABLE_SIMD: Skipping SIMD variants (no explicit AVX/NEON code paths).")
else()
# Driven by the target architectures, not the host: a universal build enables
# several of these branches at once, so they are independent ifs.
if(ZXC_TARGET_X86)
    message(STATUS "Building x86_64 AVX2 and AVX512 variants...")
    # No _sse2 variant: SSE2 is the x86-64 baseline, so _default already
    # compiles the SSE2 code paths (ZXC_USE_SSE2 in zxc_internal.h).
    if(MSVC)
        # AVX2 for MSVC (Enables AVX2/BMI1/BMI2 sets)
        zxc_add_variant(_avx2 "/arch:AVX2;/D__BMI__;/D__BMI2__;/D__LZCNT__")
        # AVX512 for MSVC (VS2019 16.10+ supports /arch:AVX512)
        zxc_add_variant(_avx512 "/arch:AVX512;/D__BMI__;/D__BMI2__;/D__LZCNT__")
    else()
        set(ZXC_AVX2_FLAGS   -mavx2 -mbmi -mbmi2 -mlzcnt)
        set(ZXC_AVX512_FLAGS -mavx512f -mavx512bw -mavx512vbmi -mavx512vbmi2 -mbmi -mbmi2 -mlzcnt)
        if(APPLE AND ZXC_TARGET_AARCH64)
            # Universal build: every variant is compiled for every slice, so the
            # x86 flags must be confined to the x86 one. -Xarch_<arch> covers the
            # single argument that follows it, hence one prefix per flag.
            list(TRANSFORM ZXC_AVX2_FLAGS   PREPEND "-Xarch_x86_64;")
            list(TRANSFORM ZXC_AVX512_FLAGS PREPEND "-Xarch_x86_64;")
        endif()
        zxc_add_variant(_avx2   "${ZXC_AVX2_FLAGS}")
        zxc_add_variant(_avx512 "${ZXC_AVX512_FLAGS}")
    endif()
endif()

if(ZXC_TARGET_AARCH64)
    message(STATUS "AArch64: NEON is baseline; no dedicated SIMD variant needed.")
endif()

if(ZXC_TARGET_ARM32)
    message(STATUS "Building ARMv7 NEON32 variant...")
    zxc_add_variant(_neon32 "-march=armv7-a;-mfpu=neon")
endif()
endif()

# Sources: exclude zxc_driver.c for Emscripten (requires pthreads + FILE* I/O).
# zxc_huffman.c lives in the per-variant build (see zxc_add_variant).
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(ZXC_CORE_SOURCES
        src/lib/zxc_common.c
        src/lib/zxc_pivco_tables.c
        src/lib/zxc_dict.c
        src/lib/zxc_dispatch.c
        src/lib/zxc_pstream.c
        src/lib/zxc_seekable.c
    )
else()
    set(ZXC_CORE_SOURCES
        src/lib/zxc_common.c
        src/lib/zxc_pivco_tables.c
        src/lib/zxc_dict.c
        src/lib/zxc_driver.c
        src/lib/zxc_dispatch.c
        src/lib/zxc_pstream.c
        src/lib/zxc_seekable.c
    )
endif()

add_library(zxc_lib
    ${ZXC_CORE_SOURCES}
    ${ZXC_VARIANT_OBJECTS}
)

# Carries the same name as the target exported by the installed package, so a
# vendored add_subdirectory() and a find_package(zxc) are interchangeable.
add_library(zxc::zxc_lib ALIAS zxc_lib)

# =============================================================================
# ABI Versioning (Debian/Linux shared libraries)
# =============================================================================
# Increment this number ONLY when breaking the ABI. 
set(ZXC_SOVERSION 4)

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
# The optimisation level comes from the build type, here as everywhere else.
# Forcing -O3 would only have covered zxc_lib: the codec itself lives in the
# variant objects, which never had it.
get_property(ZXC_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT ZXC_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_C_FLAGS MATCHES "[-/]O")
    message(WARNING "zxc: no CMAKE_BUILD_TYPE and no optimisation flag - the library will be "
                    "built unoptimised. Set CMAKE_BUILD_TYPE=Release.")
endif()

zxc_apply_common_flags(zxc_lib)

if(NOT MSVC)
    target_compile_options(zxc_lib PRIVATE $<$<BOOL:${ZXC_NATIVE_ARCH}>:-march=native>)

    # Code generation policy: the parent's business when vendored. Kept off the
    # variant objects, whose emitted code must not move.
    if(PROJECT_IS_TOP_LEVEL)
        target_compile_options(zxc_lib PRIVATE
            -fomit-frame-pointer -fstrict-aliasing -ffunction-sections -fdata-sections)
    endif()

    if(ZXC_PGO_MODE STREQUAL "GENERATE")
        message(STATUS "PGO: Generating profile data to ${ZXC_PGO_DIR}")
    elseif(ZXC_PGO_MODE STREQUAL "USE")
        if(NOT EXISTS "${ZXC_PGO_DIR}")
            message(FATAL_ERROR "PGO: Profile data not found at ${ZXC_PGO_DIR}. Run with ZXC_PGO_MODE=GENERATE first.")
        endif()
        message(STATUS "PGO: Using profile data from ${ZXC_PGO_DIR}")
    endif()

    # Linker options for Dead Code Stripping (pairs with -ffunction-sections)
    if(PROJECT_IS_TOP_LEVEL)
        if(APPLE)
            target_link_options(zxc_lib PRIVATE -Wl,-dead_strip)
        else()
            target_link_options(zxc_lib PRIVATE -Wl,--gc-sections)
        endif()
    endif()
else()
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

    # Native command-line wildcard expansion on Windows: cmd/PowerShell don't glob
    if(MSVC)
        target_link_options(zxc PRIVATE setargv.obj)
    endif()

    zxc_apply_common_flags(zxc)
    target_compile_options(zxc PRIVATE
        $<$<AND:$<NOT:$<C_COMPILER_ID:MSVC>>,$<BOOL:${ZXC_NATIVE_ARCH}>>:-march=native>)
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
    if(PROJECT_IS_TOP_LEVEL)
        enable_testing()
    endif()
    
    add_executable(zxc_test
        tests/test_main.c
        tests/test_common.c
        tests/test_buffer_api.c
        tests/test_block_api.c
        tests/test_context_api.c
        tests/test_static_ctx.c
        tests/test_pstream_api.c
        tests/test_stream_api.c
        tests/test_seekable.c
        tests/test_seekable_mt.c
        tests/test_format.c
        tests/test_misc.c
        tests/test_dict.c
    )
    
    # When building shared libraries, create a static version for tests
    # This allows tests to access internal functions for unit testing
    if(BUILD_SHARED_LIBS)
        # Create a static library specifically for tests.
        # zxc_huffman.c lives in the per-variant build (see zxc_add_variant)
        # and is already pulled in via ${ZXC_VARIANT_OBJECTS} below.
        add_library(zxc_lib_static STATIC
            src/lib/zxc_common.c
        src/lib/zxc_pivco_tables.c
            src/lib/zxc_dict.c
            src/lib/zxc_driver.c
            src/lib/zxc_dispatch.c
            src/lib/zxc_pstream.c
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
        
        # Same settings as the main library
        zxc_apply_common_flags(zxc_lib_static)
        if(NOT MSVC)
            target_compile_options(zxc_lib_static PRIVATE
                $<$<BOOL:${ZXC_NATIVE_ARCH}>:-march=native>)
            if(PROJECT_IS_TOP_LEVEL)
                target_compile_options(zxc_lib_static PRIVATE
                    -fomit-frame-pointer -fstrict-aliasing -ffunction-sections -fdata-sections)
            endif()
        endif()

        target_compile_definitions(zxc_lib_static PUBLIC ZXC_STATIC_DEFINE)
        target_link_libraries(zxc_lib_static PRIVATE Threads::Threads)
        
        # Link tests against static library
        target_link_libraries(zxc_test PRIVATE zxc_lib_static)
    else()
        # For static builds, use the main library
        target_link_libraries(zxc_test PRIVATE zxc_lib)
    endif()
    
    zxc_apply_common_flags(zxc_test)
    target_compile_options(zxc_test PRIVATE
        $<$<AND:$<NOT:$<C_COMPILER_ID:MSVC>>,$<BOOL:${ZXC_NATIVE_ARCH}>>:-march=native>)
    # Propagate definitions
    target_compile_definitions(zxc_test PRIVATE
        $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
    
    # Coverage flags for Tests
    if(ZXC_ENABLE_COVERAGE)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_link_options(zxc_test PRIVATE --coverage)
        endif()
    endif()
    
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

    # --- Conformance suite ---------------------------------------------------
    add_executable(zxc_conformance_test conformance/test_conformance.c)
    target_link_libraries(zxc_conformance_test PRIVATE zxc_lib)
    target_include_directories(zxc_conformance_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(zxc_conformance_test PRIVATE
        $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
    if(ZXC_ENABLE_COVERAGE)
        target_link_options(zxc_conformance_test PRIVATE --coverage)
    endif()

    add_test(
        NAME conformance
        COMMAND zxc_conformance_test "${CMAKE_SOURCE_DIR}/conformance"
    )

    # --- Golden-file format conformance --------------------------------------
    # Parses the byte-frozen golden files and validates every on-disk field
    # against docs/FORMAT.md. Needs the private header (static-inline hashes),
    # hence the src/lib + rapidhash include paths.
    add_executable(zxc_format_golden_test tests/format/test_golden.c)
    target_link_libraries(zxc_format_golden_test PRIVATE zxc_lib)
    target_include_directories(zxc_format_golden_test PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/lib
        ${RAPIDHASH_INCLUDE_DIR})
    target_compile_definitions(zxc_format_golden_test PRIVATE
        $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
    if(ZXC_ENABLE_COVERAGE)
        target_link_options(zxc_format_golden_test PRIVATE --coverage)
    endif()
    add_test(
        NAME format_golden
        COMMAND zxc_format_golden_test "${CMAKE_SOURCE_DIR}/tests/format/golden"
    )

    # Maintainer-only regeneration tool (public API only; not a registered test).
    add_executable(zxc_golden_gen tests/format/gen_golden.c)
    target_link_libraries(zxc_golden_gen PRIVATE zxc_lib)
    target_include_directories(zxc_golden_gen PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(zxc_golden_gen PRIVATE
        $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
    if(ZXC_ENABLE_COVERAGE)
        # Links the coverage-instrumented zxc_lib, so it needs the gcov runtime.
        target_link_options(zxc_golden_gen PRIVATE --coverage)
    endif()
endif()

# =============================================================================
# Installation
# =============================================================================
if(ZXC_INSTALL)
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
        # "unzxc" alias: a symlink to zxc that defaults to decompression. 
        # Symbolic links are POSIX-only; skipped on Windows.
        if(NOT WIN32)
            install(CODE "
                execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
                    zxc \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_BINDIR}/unzxc\")
            ")
        endif()
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
endif()

# =============================================================================
# Code Formatting (clang-format)
# =============================================================================
# Allow override via environment variable (e.g., CLANG_FORMAT=clang-format-22)
if(DEFINED ENV{CLANG_FORMAT})
    set(CLANG_FORMAT "$ENV{CLANG_FORMAT}")
else()
    find_program(CLANG_FORMAT clang-format)
endif()
if(CLANG_FORMAT AND PROJECT_IS_TOP_LEVEL)
    file(GLOB_RECURSE ZXC_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/*.h"
    )
    # Exclude vendored third-party code
    list(FILTER ZXC_FORMAT_SOURCES EXCLUDE REGEX ".*/vendors/.*")

    add_custom_target(format
        COMMAND ${CLANG_FORMAT} --style=file -i ${ZXC_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting include/, src/lib/ and src/cli/ with clang-format"
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --style=file --dry-run --Werror ${ZXC_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking formatting of include/, src/lib/ and src/cli/"
        VERBATIM
    )
elseif(PROJECT_IS_TOP_LEVEL)
    message(STATUS "clang-format not found - format/format-check targets disabled")
endif()

# =============================================================================
# Summary
# =============================================================================
message(STATUS "")
message(STATUS "ZXC Configuration Summary:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
if(PROJECT_IS_TOP_LEVEL)
    message(STATUS "  Build Mode:     Standalone")
else()
    message(STATUS "  Build Mode:     Embedded (vendored in ${CMAKE_PROJECT_NAME})")
endif()
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
message(STATUS "  Install Rules:  ${ZXC_INSTALL}")
if(ZXC_USE_SYSTEM_RAPIDHASH)
    message(STATUS "  Rapidhash:      system (${RAPIDHASH_INCLUDE_DIR})")
else()
    message(STATUS "  Rapidhash:      vendored")
endif()
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
        "_zxc_get_decompressed_size"
        "_zxc_decompress_inplace"
        "_zxc_decompress_inplace_bound"
        "_zxc_create_cctx"
        "_zxc_free_cctx"
        "_zxc_compress_cctx"
        "_zxc_create_dctx"
        "_zxc_free_dctx"
        "_zxc_decompress_dctx"
        # Push streaming API
        "_zxc_cstream_create"
        "_zxc_cstream_free"
        "_zxc_cstream_compress"
        "_zxc_cstream_end"
        "_zxc_cstream_in_size"
        "_zxc_cstream_out_size"
        "_zxc_dstream_create"
        "_zxc_dstream_free"
        "_zxc_dstream_decompress"
        "_zxc_dstream_finished"
        "_zxc_dstream_in_size"
        "_zxc_dstream_out_size"
        # Seekable API
        "_zxc_seekable_open"
        "_zxc_seekable_free"
        "_zxc_seekable_get_num_blocks"
        "_zxc_seekable_get_decompressed_size"
        "_zxc_seekable_get_block_comp_size"
        "_zxc_seekable_get_block_decomp_size"
        "_zxc_seekable_set_dict"
        # Dictionary API
        "_zxc_train_dict"
        "_zxc_train_dict_huf"
        "_zxc_dict_train"
        "_zxc_dict_id"
        "_zxc_get_dict_id"
        "_zxc_dict_get_id"
        "_zxc_dict_save"
        "_zxc_dict_save_bound"
        "_zxc_dict_load"
        "_zxc_dict_huf"
        # i32-offset shim for seekable_decompress_range (see wasm_entry.c);
        # avoids the i64 offset arg which cwrap cannot pass without BigInt.
        "_zxcw_seekable_decompress_range"
        "_zxc_write_seek_table"
        "_zxc_seek_table_size"
        "_zxc_min_level"
        "_zxc_max_level"
        "_zxc_default_level"
        "_zxc_version_string"
        "_zxc_error_name"
        # Options-struct layout guards
        "_zxc_compress_opts_size"
        "_zxc_decompress_opts_size"
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
        "-sEXPORTED_RUNTIME_METHODS=[ccall,cwrap,UTF8ToString,getTempRet0,HEAPU8,HEAP32,HEAPU32]"
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sEXPORT_NAME=ZXCModule"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=2097152"
        "-sSTACK_SIZE=131072"
        "-sENVIRONMENT=web,node"
        "-sNO_FILESYSTEM=1"
        "-sSTRICT=1"
        "-sWASM_BIGINT=0"
    )
    target_compile_options(zxc_wasm PRIVATE -O3)

    message(STATUS "  WASM Target:    zxc.js + zxc.wasm")
endif()

# =============================================================================
# Documentation (Doxygen)
# =============================================================================
# Maintainer tooling: a generic target name an embedding project may own too.
if(PROJECT_IS_TOP_LEVEL)
    find_package(Doxygen)
endif()
if(DOXYGEN_FOUND AND PROJECT_IS_TOP_LEVEL)
    # Generate the Doxyfile with the current project version
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
    
    # Add a 'doc' target to generate documentation (e.g. 'make doc' or 'cmake --build . --target doc')
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()
