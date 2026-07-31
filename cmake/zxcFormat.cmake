# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# Code Formatting (clang-format).

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
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
    )
    # Exclude vendored third-party code
    list(FILTER ZXC_FORMAT_SOURCES EXCLUDE REGEX ".*/vendors/.*")

    add_custom_target(format
        COMMAND ${CLANG_FORMAT} --style=file -i ${ZXC_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting include/, src/lib/, src/cli/ and tests/ with clang-format"
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT} --style=file --dry-run --Werror ${ZXC_FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking formatting of include/, src/lib/, src/cli/ and tests/"
        VERBATIM
    )
elseif(PROJECT_IS_TOP_LEVEL)
    message(STATUS "clang-format not found - format/format-check targets disabled")
endif()
