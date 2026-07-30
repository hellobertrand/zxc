# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# Unit tests, conformance suite and golden-file format tests.

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
