cmake_minimum_required(VERSION 3.20)
project(zxc_python C)

find_package(Python COMPONENTS Development.Module REQUIRED)

# ---- Core ----
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../
                 ${CMAKE_CURRENT_BINARY_DIR}/zxc_core_build)

# ---- Python extension ----
Python_add_library(_zxc MODULE src/zxc/_zxc.c)

# Without "lib" prefix
set_target_properties(_zxc PROPERTIES PREFIX "")

target_include_directories(_zxc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(_zxc PRIVATE zxc_lib)

install(TARGETS _zxc DESTINATION zxc)