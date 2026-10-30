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
set(EB_PREGENERATED_DIR "${CMAKE_CURRENT_LIST_DIR}/../generated" CACHE PATH "Use checked-in generated sources for Vita builds" FORCE)
set(EB_BUILD_VITA ON CACHE BOOL "Build the Vita SDL frontend" FORCE)

option(EB_VITA_COMPACT_SPC_DISPATCH "Use compact SPC source-site dispatch on this experimental branch" ON)
set(EB_COMPACT_SPC_DISPATCH ${EB_VITA_COMPACT_SPC_DISPATCH} CACHE BOOL "Vita SPC dispatch selection" FORCE)

option(EB_VITA_RUNTIME_TIMING "Sample core CPU/PPU/DSP timings every 61 updates" ON)
if(EB_VITA_RUNTIME_TIMING)
    add_compile_definitions(EB_RUNTIME_TIMING)
endif()

# Apply IPO to the shared core as well as the frontend. Fall back cleanly for
# SDK/compiler versions whose linker cannot perform LTO.
option(EB_VITA_LTO "Enable Release link-time optimisation across game libraries" ON)
if(EB_VITA_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT EB_VITA_IPO_SUPPORTED OUTPUT EB_VITA_IPO_ERROR LANGUAGES CXX)
    if(EB_VITA_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(WARNING "Vita LTO unavailable: ${EB_VITA_IPO_ERROR}")
    endif()
endif()

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../cpp" "${CMAKE_CURRENT_BINARY_DIR}/phase-distorter-core")
