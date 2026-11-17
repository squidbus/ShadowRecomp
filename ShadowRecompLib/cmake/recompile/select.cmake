# Chooses which XboxRecomp2 binary generates the C++.

include("${CMAKE_CURRENT_LIST_DIR}/select/build_from_source.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/select/download.cmake")

# Variables
set(SHADOW_RECOMP_XBOXRECOMP2 "auto" CACHE STRING
    "XboxRecomp2 to use: `auto`, `source` (build tools/XboxRecomp2) or `binary` (download a release)")
set_property(CACHE SHADOW_RECOMP_XBOXRECOMP2 PROPERTY STRINGS auto source binary)
set(SHADOW_RECOMP_XBOXRECOMP2_VERSION "0.5.1" CACHE STRING
    "XboxRecomp2 release to download in `binary` mode: a tag like `0.5.1`, or `latest`")
set(SHADOW_RECOMP_XBOXRECOMP2_EXECUTABLE "" CACHE FILEPATH
    "Existing XboxRecomp2 binary. Overrides SHADOW_RECOMP_XBOXRECOMP2 when set")

# Finds or builds the XboxRecomp2 binary.
#
# Arguments:
# - out_executable: variable that receives the binary path.
#
# Order:
# - SHADOW_RECOMP_XBOXRECOMP2_EXECUTABLE, when set.
# - SHADOW_RECOMP_XBOXRECOMP2 = `source` or `binary`.
# - `auto`: `source` when tools/XboxRecomp2 is checked out and cargo is found, else `binary`.
#
# `binary` builds from source when the download fails and a source build is possible.
function(xboxrecomp2_select out_executable)
    # Use existing binary if specified.
    if(SHADOW_RECOMP_XBOXRECOMP2_EXECUTABLE)
        if(NOT EXISTS "${SHADOW_RECOMP_XBOXRECOMP2_EXECUTABLE}")
            message(FATAL_ERROR "SHADOW_RECOMP_XBOXRECOMP2_EXECUTABLE is set to ${SHADOW_RECOMP_XBOXRECOMP2_EXECUTABLE}, which doesn't exist.")
        endif()
        message(STATUS "XboxRecomp2: using ${SHADOW_RECOMP_XBOXRECOMP2_EXECUTABLE}")
        set(${out_executable} "${SHADOW_RECOMP_XBOXRECOMP2_EXECUTABLE}" PARENT_SCOPE)
        return()
    endif()

    # Check whether we can build from source.
    find_program(SHADOW_RECOMP_CARGO cargo)
    if(EXISTS "${SHADOW_RECOMP_TOOLS_ROOT}/XboxRecomp2/Cargo.toml" AND SHADOW_RECOMP_CARGO)
        set(can_build TRUE)
    else()
        set(can_build FALSE)
    endif()

    # Resolve `auto`.
    set(mode "${SHADOW_RECOMP_XBOXRECOMP2}")
    if(mode STREQUAL "auto")
        if(can_build)
            set(mode "source")
            set(reason "tools/XboxRecomp2 is checked out and cargo was found")
        else()
            set(mode "binary")
            set(reason "tools/XboxRecomp2 isn't checked out or cargo wasn't found")
        endif()
        message(STATUS "XboxRecomp2: auto picked `${mode}` (${reason})")
    endif()

    # Get the binary.
    if(mode STREQUAL "source")
        xboxrecomp2_build_from_source(executable)
    elseif(mode STREQUAL "binary")
        _xboxrecomp2_download_or_build("${can_build}" executable)
    else()
        message(FATAL_ERROR "SHADOW_RECOMP_XBOXRECOMP2 must be `auto`, `source` or `binary`, not `${mode}`.")
    endif()

    message(STATUS "XboxRecomp2: ${executable}")
    set(${out_executable} "${executable}" PARENT_SCOPE)
endfunction()

# Downloads the release binary, or builds from source if that fails.
function(_xboxrecomp2_download_or_build can_build out_executable)
    set(version "${SHADOW_RECOMP_XBOXRECOMP2_VERSION}")
    xboxrecomp2_download("${version}" "${CMAKE_SOURCE_DIR}/out/xr2-bin" executable error)

    if(error)
        # Build from source instead.
        if(NOT can_build)
            message(FATAL_ERROR
                "XboxRecomp2 ${version}: ${error}.\n"
                "To build it from source instead, check out tools/XboxRecomp2 and install Rust. "
                "Or set SHADOW_RECOMP_XBOXRECOMP2_EXECUTABLE to a binary you already have.")
        endif()
        message(STATUS "XboxRecomp2 ${version}: ${error}. Building from source instead.")
        xboxrecomp2_build_from_source(executable)
    endif()

    set(${out_executable} "${executable}" PARENT_SCOPE)
endfunction()
