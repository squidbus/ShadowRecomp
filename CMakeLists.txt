# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.20)

if(NOT DEFINED ENV{VCPKG_ROOT})
    message(FATAL_ERROR "VCPKG_ROOT is not defined!")
endif()

set(SHADOW_RECOMP_THIRDPARTY_ROOT ${CMAKE_SOURCE_DIR}/thirdparty)
set(SHADOW_RECOMP_TOOLS_ROOT ${CMAKE_SOURCE_DIR}/tools)
set(CMAKE_CXX_STANDARD 20)
set(BUILD_SHARED_LIBS OFF)
set(VCPKG_APPLOCAL_DEPS OFF)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo,Release>:ProgramDatabase>>")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project ("ShadowRecomp-ALL")

# Ship symbols with release builds (for now)
if (MSVC)
    # To generate PDB needs DEBUG, but doing so disables REF, ICF (re-enabling).
    add_link_options("$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
endif()

if (CMAKE_OSX_ARCHITECTURES)
    set(SHADOW_RECOMP_ARCHITECTURE ${CMAKE_OSX_ARCHITECTURES})
elseif(CMAKE_SYSTEM_PROCESSOR)
    set(SHADOW_RECOMP_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
else()
    set(SHADOW_RECOMP_ARCHITECTURE ${CMAKE_HOST_SYSTEM_PROCESSOR})
endif()
string(TOLOWER "${SHADOW_RECOMP_ARCHITECTURE}" SHADOW_RECOMP_ARCHITECTURE)
message(STATUS "Detected architecture: ${SHADOW_RECOMP_ARCHITECTURE}")

if (SHADOW_RECOMP_ARCHITECTURE STREQUAL "x86_64" OR SHADOW_RECOMP_ARCHITECTURE STREQUAL "amd64")
    add_compile_options(
        -march=sandybridge # You have a puter made after 2011, right?
        -mtune=znver4      # Most probably have one circa 2022-2023
    )
endif()

# Sewer says: Don't clear used registers before every return.
# Some toolchains (e.g. Nix's clang wrapper) turn this on by default.
# It adds a few `xor`s to every function: ~1.8% code size, with only speed loss.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-fzero-call-used-regs=skip SHADOW_RECOMP_HAS_ZERO_CALL_USED_REGS)
if (SHADOW_RECOMP_HAS_ZERO_CALL_USED_REGS)
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-fzero-call-used-regs=skip>)
endif()


add_subdirectory(${SHADOW_RECOMP_THIRDPARTY_ROOT})
add_subdirectory(${SHADOW_RECOMP_TOOLS_ROOT})

# Include sub-projects.
add_subdirectory("ShadowRecompLib")
add_subdirectory ("ShadowRecomp")
