# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# CLI Executable.

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
