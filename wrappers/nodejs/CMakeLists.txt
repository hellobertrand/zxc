cmake_minimum_required(VERSION 3.14)
project(zxc_nodejs C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Core ZXC library ----
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../
                 ${CMAKE_CURRENT_BINARY_DIR}/zxc_core_build)

# ---- Node.js addon ----
include_directories(${CMAKE_JS_INC})

file(GLOB SOURCE_FILES "src/zxc_addon.cc")

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".node"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_JS_INC}
)

# Generate node.lib on Windows (cmake-js provides .def but not .lib)
if(MSVC AND CMAKE_JS_NODELIB_DEF AND CMAKE_JS_NODELIB_TARGET)
    execute_process(COMMAND ${CMAKE_AR} /def:${CMAKE_JS_NODELIB_DEF}
                    /out:${CMAKE_JS_NODELIB_TARGET}
                    ${CMAKE_STATIC_LINKER_FLAGS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    zxc_lib
    ${CMAKE_JS_LIB}
)

# node-addon-api (header-only, no exceptions by default)
execute_process(
    COMMAND node -p "require('node-addon-api').include"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE NAPI_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
# Strip surrounding quotes if present
string(REPLACE "\"" "" NAPI_INCLUDE_DIR ${NAPI_INCLUDE_DIR})

target_include_directories(${PROJECT_NAME} PRIVATE ${NAPI_INCLUDE_DIR})
target_compile_definitions(${PROJECT_NAME} PRIVATE NAPI_VERSION=8)
