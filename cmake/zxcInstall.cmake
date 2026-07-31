# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# Installation: headers, pkg-config and the CMake package for find_package(zxc).

if(ZXC_INSTALL)
    include(GNUInstallDirs)

    # A pkg-config consumer gets no CMake usage requirements, so the .pc must
    # carry them: the export macro matching the build, and -pthread only when
    # FindThreads actually picked pthreads (MSVC ignores it with LNK4044).
    if(BUILD_SHARED_LIBS)
        set(ZXC_PC_CFLAGS "-DZXC_DLL_IMPORT")
    else()
        set(ZXC_PC_CFLAGS "-DZXC_STATIC_DEFINE")
    endif()
    if(CMAKE_USE_PTHREADS_INIT)
        set(ZXC_PC_LIBS_PRIVATE "-pthread")
    else()
        set(ZXC_PC_LIBS_PRIVATE "")
    endif()

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
        # \${CMAKE_INSTALL_PREFIX} is escaped so it expands at install time:
        # CMAKE_INSTALL_FULL_BINDIR bakes the configure-time prefix and would
        # ignore `cmake --install --prefix`.
        if(NOT WIN32)
            if(IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}")
                set(ZXC_SYMLINK_BINDIR "${CMAKE_INSTALL_BINDIR}")
            else()
                set(ZXC_SYMLINK_BINDIR "\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}")
            endif()
            install(CODE "
                execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
                    zxc \"\$ENV{DESTDIR}${ZXC_SYMLINK_BINDIR}/unzxc\")
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
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/zxcConfig.cmake.in
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
