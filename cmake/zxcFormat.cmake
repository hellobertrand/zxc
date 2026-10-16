# SPDX-License-Identifier: BSD-3-Clause
# ZXC - High-performance lossless compression
#
# Copyright (c) Bertrand Lebonnois and contributors.
#
# Code Formatting (clang-format).

# .clang-format uses NumericLiteralCase, added in clang-format 22.
set(ZXC_CLANG_FORMAT_MIN 22)

# Allow override via environment variable (e.g., CLANG_FORMAT=clang-format-22)
if(DEFINED ENV{CLANG_FORMAT})
    set(CLANG_FORMAT "$ENV{CLANG_FORMAT}")
else()
    find_program(CLANG_FORMAT NAMES clang-format-${ZXC_CLANG_FORMAT_MIN} clang-format)
endif()

if(CLANG_FORMAT AND PROJECT_IS_TOP_LEVEL)
    execute_process(COMMAND ${CLANG_FORMAT} --version
        OUTPUT_VARIABLE ZXC_CLANG_FORMAT_OUT ERROR_QUIET RESULT_VARIABLE ZXC_CLANG_FORMAT_RC)
    string(REGEX MATCH "version ([0-9]+(\\.[0-9]+)*)" _ "${ZXC_CLANG_FORMAT_OUT}")
    set(ZXC_CLANG_FORMAT_VERSION "${CMAKE_MATCH_1}")
    if(NOT ZXC_CLANG_FORMAT_RC EQUAL 0 OR NOT ZXC_CLANG_FORMAT_VERSION)
        set(ZXC_CLANG_FORMAT_VERSION "unknown version")
    endif()
endif()

if(CLANG_FORMAT AND PROJECT_IS_TOP_LEVEL AND
   (ZXC_CLANG_FORMAT_VERSION STREQUAL "unknown version" OR
    ZXC_CLANG_FORMAT_VERSION VERSION_LESS ZXC_CLANG_FORMAT_MIN))
    # Only the format targets fail: building the library never needs clang-format.
    set(ZXC_CLANG_FORMAT_ERROR
        "Error: clang-format >= ${ZXC_CLANG_FORMAT_MIN} required, found ${CLANG_FORMAT} (${ZXC_CLANG_FORMAT_VERSION}); set CLANG_FORMAT=/path/to/clang-format-${ZXC_CLANG_FORMAT_MIN}")
    message(STATUS "${ZXC_CLANG_FORMAT_ERROR} - format/format-check targets will fail")
    foreach(target format format-check)
        add_custom_target(${target}
            COMMAND ${CMAKE_COMMAND} -E echo "${ZXC_CLANG_FORMAT_ERROR}"
            COMMAND ${CMAKE_COMMAND} -E false
            VERBATIM
        )
    endforeach()
elseif(CLANG_FORMAT AND PROJECT_IS_TOP_LEVEL)
    file(GLOB_RECURSE ZXC_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lib/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/conformance/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/wrappers/nodejs/src/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/wrappers/nodejs/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/wrappers/python/src/zxc/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/wrappers/python/src/zxc/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/wrappers/wasm/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/wrappers/wasm/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/contrib/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/contrib/*.h"
    )
    # Exclude vendored third-party code
    list(FILTER ZXC_FORMAT_SOURCES EXCLUDE REGEX ".*/vendors/.*")
    list(FILTER ZXC_FORMAT_SOURCES EXCLUDE REGEX
        ".*/(node_modules|\\.venv|target|build|__pycache__)/.*")

    add_custom_target(format
        COMMAND ${CLANG_FORMAT} --style=file -i ${ZXC_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting include/, src/, tests/, conformance/, wrappers/ and contrib/ with clang-format"
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --style=file --dry-run --Werror ${ZXC_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking formatting of include/, src/, tests/, conformance/, wrappers/ and contrib/"
        VERBATIM
    )
elseif(PROJECT_IS_TOP_LEVEL)
    message(STATUS "clang-format not found - format/format-check targets disabled")
endif()
