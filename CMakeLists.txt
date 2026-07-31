# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.14)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Version extraction from include/zxc_constants.h; must run before project().
include(zxcVersion)

project(zxc
    VERSION ${MAJOR_VER}.${MINOR_VER}.${PATCH_VER}
    LANGUAGES C
    DESCRIPTION "High-performance asymmetric lossless compression library"
)

include(zxcOptions)         # Build options
include(zxcPlatform)        # Target architecture + Emscripten/WASM overrides
include(zxcCompilerFlags)   # C standard, LTO/PGO, per-target flag helpers
include(zxcDependencies)    # Rapidhash: system-installed or vendored
include(zxcVariants)        # Function multi-versioning (SIMD variant objects)

# =============================================================================
# Core Library & Runtime Dispatch
# =============================================================================
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
    target_compile_definitions(zxc_lib INTERFACE ZXC_DLL_IMPORT)
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

include(zxcCli)             # CLI executable
include(zxcTests)           # Unit, conformance and golden-file tests
include(zxcInstall)         # Install rules (headers, pkg-config, CMake package)
include(zxcFormat)          # clang-format format/format-check targets
include(zxcSummary)         # Configuration summary
include(zxcWasm)            # WebAssembly (Emscripten) target
include(zxcDocs)            # Doxygen 'doc' target
