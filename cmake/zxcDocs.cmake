# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# Documentation (Doxygen).

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
