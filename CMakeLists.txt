# gscope: a jetson_stats-style system monitor served over HTTP/WebSocket,
# built on libhttp; the metrics travel as librpc calls (libbufs messages).
# The Angular frontend is built separately (npm).

# 3.16: librpc needs it. The range turns on the newer policies (CMP0116, for
# the DEPFILEs of rpcc and bufsc) where CMake has them.
cmake_minimum_required(VERSION 3.16...3.28)
project(gscope LANGUAGES CXX)

# A build configured without a type gets Release: optimized code, and smaller binaries.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

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

# libcore: a sibling checkout by default; ignored when the including project already provides the targets.
set(GSCOPE_LIBCORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../libcore" CACHE PATH "Path to a libcore checkout")
if(NOT TARGET core::sys)
    set(CORE_BUILD_TESTS OFF CACHE BOOL "")
    add_subdirectory("${GSCOPE_LIBCORE_DIR}" libcore)
endif()

if(NOT TARGET http)
    set(HTTP_BUILD_EXAMPLES OFF CACHE BOOL "")
    set(HTTP_BUILD_TESTS OFF CACHE BOOL "")
    add_subdirectory("${GSCOPE_LIBHTTP_DIR}" libhttp)
endif()

# librpc carries the metrics (backend/include/gscope/wire/monitor.bufs): its
# rpcc writes the C++ server stub and the frontend's TypeScript client. A
# sibling checkout by default; it brings libbufs along and reuses the
# libhttp and libcore above.
set(GSCOPE_LIBRPC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../librpc" CACHE PATH "Path to a librpc checkout")
if(NOT TARGET rpc)
    set(RPC_BUILD_TESTS OFF CACHE BOOL "")
    set(RPC_BUILD_EXAMPLES OFF CACHE BOOL "")
    add_subdirectory("${GSCOPE_LIBRPC_DIR}" librpc)
endif()

add_subdirectory(backend)
