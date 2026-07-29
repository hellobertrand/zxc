# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# Configuration summary.

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
