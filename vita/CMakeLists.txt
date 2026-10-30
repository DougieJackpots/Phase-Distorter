cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "Set VITASDK to your VitaSDK installation before configuring.")
endif()
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake"
        CACHE FILEPATH "VitaSDK CMake toolchain")
endif()

project(PhaseDistorterVita LANGUAGES C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(EB_BUILD_FRONTEND OFF CACHE BOOL "Disable desktop SDL/OpenGL frontend" FORCE)
set(EB_BUILD_TESTS OFF CACHE BOOL "Disable desktop verification targets" FORCE)
set(EB_BUILD_VITA ON CACHE BOOL "Build the Vita SDL frontend" FORCE)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../cpp" "${CMAKE_CURRENT_BINARY_DIR}/phase-distorter-core")
