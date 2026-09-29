# gscope: a jetson_stats-style system monitor served over HTTP/WebSocket,
# built on libhttp. The Angular frontend is built separately (npm).

cmake_minimum_required(VERSION 3.15)
project(gscope LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GSCOPE_BUILD_TESTS "Build the gscope backend tests" ON)
# A sibling checkout by default; ignored when the including project already
# provides the `http` target.
set(GSCOPE_LIBHTTP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libhttp" CACHE PATH "Path to a libhttp checkout")

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    include(CTest)
endif()

if(NOT TARGET http)
    set(HTTP_BUILD_EXAMPLES OFF CACHE BOOL "")
    set(HTTP_BUILD_TESTS OFF CACHE BOOL "")
    add_subdirectory("${GSCOPE_LIBHTTP_DIR}" libhttp)
endif()

add_subdirectory(backend)
