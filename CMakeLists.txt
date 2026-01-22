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
option(ZXC_ENABLE_LTO "Enable Interprocedural Optimization (LTO)" ON)
set(ZXC_PGO_MODE "OFF" CACHE STRING "Profile-Guided Optimization mode (OFF/GENERATE/USE)")
set_property(CACHE ZXC_PGO_MODE PROPERTY STRINGS OFF GENERATE USE)
option(ZXC_BUILD_CLI "Build the command-line interface" ON)
option(ZXC_BUILD_TESTS "Build unit tests" ON)

# =============================================================================
# C Standard
# =============================================================================
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Check for LTO support
if(ZXC_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT result OUTPUT output)
    if(result)
        message(STATUS "LTO/IPO is supported and enabled.")
    else()
        message(WARNING "LTO/IPO is not supported: ${output}")
        set(ZXC_ENABLE_LTO OFF)
    endif()
endif()

# =============================================================================
# Core Library & Runtime Dispatch
# =============================================================================

# Function Multi-Versioning Helper
# Compiles src/lib/zxc_compress.c and src/lib/zxc_decompress.c with specific flags and suffix.
macro(zxc_add_variant suffix flags)
    add_library(zxc_compress${suffix} OBJECT src/lib/zxc_compress.c)
    target_compile_options(zxc_compress${suffix} PRIVATE ${flags})
    target_compile_definitions(zxc_compress${suffix} PRIVATE ZXC_FUNCTION_SUFFIX=${suffix})
    # Inherit include directories
    target_include_directories(zxc_compress${suffix} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/lib PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

    add_library(zxc_decompress${suffix} OBJECT src/lib/zxc_decompress.c)
    target_compile_options(zxc_decompress${suffix} PRIVATE ${flags})
    target_compile_definitions(zxc_decompress${suffix} PRIVATE ZXC_FUNCTION_SUFFIX=${suffix})
    target_include_directories(zxc_decompress${suffix} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/lib PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    
    list(APPEND ZXC_VARIANT_OBJECTS $<TARGET_OBJECTS:zxc_compress${suffix}> $<TARGET_OBJECTS:zxc_decompress${suffix}>)
endmacro()

set(ZXC_VARIANT_OBJECTS "")

# --- 1. Default Variant (Scalar/Baseline) ---
zxc_add_variant(_default "")

# --- 2. Architecture Specific Variants ---
if(CMAKE_SYSTEM_PROCESSOR MATCHES "amd64|x86_64|AMD64")
    message(STATUS "Building x86_64 AVX2 and AVX512 variants...")
    if(MSVC)
        # AVX2 for MSVC (Enables AVX2/BMI2 sets)
        zxc_add_variant(_avx2 "/arch:AVX2;/D__BMI2__")
        # AVX512 for MSVC (VS2019 16.10+ supports /arch:AVX512)
        zxc_add_variant(_avx512 "/arch:AVX512;/D__BMI2__")
    else()
        # AVX2 for GCC/Clang
        zxc_add_variant(_avx2 "-mavx2;-mfma;-mbmi2")
        # AVX512 for GCC/Clang
        zxc_add_variant(_avx512 "-mavx512f;-mavx512bw;-mbmi2")
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

add_library(zxc_lib STATIC
    src/lib/zxc_common.c
    src/lib/zxc_driver.c
    src/lib/zxc_dispatch.c
    ${ZXC_VARIANT_OBJECTS}
)

# Target-based include directories for the main lib
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
if(NOT MSVC)
    target_compile_options(zxc_lib PRIVATE
        -O3 
        -Wall -Wextra 
        -fomit-frame-pointer 
        -fstrict-aliasing
        # Native Arch
        $<$<BOOL:${ZXC_NATIVE_ARCH}>:-march=native>
        # Dead code elimination
        -ffunction-sections -fdata-sections
    )
    
    # Profile-Guided Optimization
    if(ZXC_PGO_MODE STREQUAL "GENERATE")
        target_compile_options(zxc_lib PRIVATE -fprofile-generate=${CMAKE_BINARY_DIR}/pgo)
        target_link_options(zxc_lib PRIVATE -fprofile-generate=${CMAKE_BINARY_DIR}/pgo)
        message(STATUS "PGO: Generating profile data to ${CMAKE_BINARY_DIR}/pgo")
    elseif(ZXC_PGO_MODE STREQUAL "USE")
        if(EXISTS "${CMAKE_BINARY_DIR}/pgo")
            target_compile_options(zxc_lib PRIVATE -fprofile-use=${CMAKE_BINARY_DIR}/pgo -fprofile-correction)
            target_link_options(zxc_lib PRIVATE -fprofile-use=${CMAKE_BINARY_DIR}/pgo)
            message(STATUS "PGO: Using profile data from ${CMAKE_BINARY_DIR}/pgo")
        else()
            message(FATAL_ERROR "PGO: Profile data not found. Run with ZXC_PGO_MODE=GENERATE first.")
        endif()
    endif()

    # Linker options for Dead Code Stripping
    if(APPLE)
        target_link_options(zxc_lib PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(zxc_lib PRIVATE -Wl,--gc-sections)
    endif()
else()
    target_compile_options(zxc_lib PRIVATE /O2 /W3)
    target_compile_definitions(zxc_lib PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_compile_definitions(zxc_lib PRIVATE
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:_GNU_SOURCE>
)

# Enable LTO cleanly
if(ZXC_ENABLE_LTO)
    set_property(TARGET zxc_lib PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    if(NOT MSVC)
        target_compile_options(zxc_lib PRIVATE -flto)
        target_link_options(zxc_lib PRIVATE -flto)
    endif()
endif()

# Threading support
find_package(Threads REQUIRED)
target_link_libraries(zxc_lib PRIVATE Threads::Threads)

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

    # Enable LTO cleanly
    if(ZXC_ENABLE_LTO)
        set_property(TARGET zxc PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        if(NOT MSVC)
            target_compile_options(zxc PRIVATE -flto)
            target_link_options(zxc PRIVATE -flto)
        endif()
    endif()
    
    # Profile-Guided Optimization for CLI
    if(NOT MSVC)
        if(ZXC_PGO_MODE STREQUAL "GENERATE")
            target_compile_options(zxc PRIVATE -fprofile-generate=${CMAKE_BINARY_DIR}/pgo)
            target_link_options(zxc PRIVATE -fprofile-generate=${CMAKE_BINARY_DIR}/pgo)
        elseif(ZXC_PGO_MODE STREQUAL "USE")
            target_compile_options(zxc PRIVATE -fprofile-use=${CMAKE_BINARY_DIR}/pgo -fprofile-correction)
            target_link_options(zxc PRIVATE -fprofile-use=${CMAKE_BINARY_DIR}/pgo)
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
    enable_testing()
    
    add_executable(zxc_test tests/test.c)
    target_link_libraries(zxc_test PRIVATE zxc_lib)
    
    # Propagate compile options
    target_compile_options(zxc_test PRIVATE
        $<$<AND:$<NOT:$<C_COMPILER_ID:MSVC>>,$<BOOL:${ZXC_NATIVE_ARCH}>>:-march=native>
    )
    
    # Profile-Guided Optimization for tests
    if(NOT MSVC)
        if(ZXC_PGO_MODE STREQUAL "GENERATE")
            target_compile_options(zxc_test PRIVATE -fprofile-generate=${CMAKE_BINARY_DIR}/pgo)
            target_link_options(zxc_test PRIVATE -fprofile-generate=${CMAKE_BINARY_DIR}/pgo)
        elseif(ZXC_PGO_MODE STREQUAL "USE")
            target_compile_options(zxc_test PRIVATE -fprofile-use=${CMAKE_BINARY_DIR}/pgo -fprofile-correction)
            target_link_options(zxc_test PRIVATE -fprofile-use=${CMAKE_BINARY_DIR}/pgo)
        endif()
    endif()
    
    target_include_directories(zxc_test PRIVATE src/lib)
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
message(STATUS "  LTO Enabled:    ${ZXC_ENABLE_LTO}")
message(STATUS "  PGO Mode:       ${ZXC_PGO_MODE}")
message(STATUS "  Build CLI:      ${ZXC_BUILD_CLI}")
message(STATUS "  Build Tests:    ${ZXC_BUILD_TESTS}")
message(STATUS "")
