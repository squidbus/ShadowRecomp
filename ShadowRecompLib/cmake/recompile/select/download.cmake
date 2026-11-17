# Downloads a prebuilt XboxRecomp2 release binary.

set(XBOXRECOMP2_REPOSITORY "Sewer56/XboxRecomp2")

# Release assets. From Sewer's `devops-rust-lightweight-binary` action.
# Matches tools/XboxRecomp2/.github/workflows/rust.yml.
set(XBOXRECOMP2_ASSETS
    linux-x64 linux-x86 linux-arm64 linux-armv7-hf
    windows-x64 windows-x86 windows-arm64
    macos-x64 macos-arm64
)

# Downloads the XboxRecomp2 binary for this host.
#
# Arguments:
# - version: release tag like `0.5.1`, or `latest`.
# - destination: download folder. Each version gets a subfolder.
# - out_executable: variable that receives the binary path.
# - out_error: variable that receives why the download failed, or empty.
function(xboxrecomp2_download version destination out_executable out_error)
    set(${out_executable} "" PARENT_SCOPE)
    set(${out_error} "" PARENT_SCOPE)

    # Pick the release asset for this host.
    cmake_host_system_information(RESULT host_arch QUERY OS_PLATFORM)
    _xboxrecomp2_asset_for("${CMAKE_HOST_SYSTEM_NAME}" "${host_arch}" asset)

    if(NOT asset)
        set(${out_error} "no prebuilt binary for ${CMAKE_HOST_SYSTEM_NAME} ${host_arch}" PARENT_SCOPE)
        return()
    endif()
    set(executable_name "xboxrecomp2")
    if(CMAKE_HOST_WIN32)
        string(APPEND executable_name ".exe")
    endif()

    # Reuse a downloaded pinned version.
    # `latest` is always downloaded again.
    set(version_dir "${destination}/${version}")
    set(executable "${version_dir}/${executable_name}")
    if(NOT version STREQUAL "latest" AND EXISTS "${executable}")
        set(${out_executable} "${executable}" PARENT_SCOPE)
        return()
    endif()

    # Download the archive.
    set(archive "${destination}/${version}-${asset}")
    file(MAKE_DIRECTORY "${destination}")
    _xboxrecomp2_fetch("${version}" "${asset}" "${archive}" error)
    if(error)
        set(${out_error} "${error}" PARENT_SCOPE)
        return()
    endif()

    # Unpack into a clean folder.
    file(REMOVE_RECURSE "${version_dir}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${version_dir}")
    file(REMOVE "${archive}")
    if(NOT EXISTS "${executable}")
        set(${out_error} "${asset} from release ${version} has no ${executable_name}" PARENT_SCOPE)
        return()
    endif()

    # 7z doesn't keep the executable bit.
    file(CHMOD "${executable}" PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    set(${out_executable} "${executable}" PARENT_SCOPE)
endfunction()

# Sets `out_asset` to the release asset for an OS and CPU, or empty if none.
#
# `os` is a CMAKE_HOST_SYSTEM_NAME value.
# `arch` is `uname -m`, or PROCESSOR_ARCHITECTURE on Windows.
function(_xboxrecomp2_asset_for os arch out_asset)
    set(${out_asset} "" PARENT_SCOPE)

    # OS
    if(os STREQUAL "Windows")
        set(os_name "windows")
    elseif(os STREQUAL "Linux")
        set(os_name "linux")
    elseif(os STREQUAL "Darwin")
        set(os_name "macos")
    else()
        return()
    endif()

    # CPU
    string(TOLOWER "${arch}" arch)
    if(arch MATCHES "^(x86_64|amd64)$")
        set(cpu_name "x64")
    elseif(arch MATCHES "^(i[3-6]86|x86)$")
        set(cpu_name "x86")
    elseif(arch MATCHES "^(aarch64|arm64)$")
        set(cpu_name "arm64")
    elseif(arch MATCHES "^armv7")
        set(cpu_name "armv7-hf")
    else()
        return()
    endif()

    # Only some OS and CPU pairs have a binary, e.g. no `windows-arm64`.
    set(asset "${os_name}-${cpu_name}")
    if(asset IN_LIST XBOXRECOMP2_ASSETS)
        set(${out_asset} "${asset}.7z" PARENT_SCOPE)
    endif()
endfunction()

# Downloads a release asset to `archive`.
# Sets `out_error` to why the download failed, or empty.
function(_xboxrecomp2_fetch version asset archive out_error)
    set(${out_error} "" PARENT_SCOPE)

    # Public link.
    if(version STREQUAL "latest")
        set(url "https://github.com/${XBOXRECOMP2_REPOSITORY}/releases/latest/download/${asset}")
    else()
        set(url "https://github.com/${XBOXRECOMP2_REPOSITORY}/releases/download/${version}/${asset}")
    endif()
    file(DOWNLOAD "${url}" "${archive}" STATUS status)

    list(GET status 0 status_code)
    if(status_code EQUAL 0)
        return()
    endif()
    list(GET status 1 status_message)
    file(REMOVE "${archive}")

    # HACK: Remove when making repos public.
    #       Right now we kinda need auth.
    find_program(XBOXRECOMP2_GH gh)
    if(NOT XBOXRECOMP2_GH)
        set(${out_error} "can't download ${url} (${status_message}), and the GitHub CLI (gh) isn't installed" PARENT_SCOPE)
        return()
    endif()

    # `gh` takes no tag for the latest release.
    if(version STREQUAL "latest")
        set(tag_args "")
    else()
        set(tag_args "${version}")
    endif()
    execute_process(
        COMMAND "${XBOXRECOMP2_GH}" release download ${tag_args}
            --repo "${XBOXRECOMP2_REPOSITORY}" --pattern "${asset}" --output "${archive}" --clobber
        RESULT_VARIABLE gh_result
        ERROR_VARIABLE gh_error
    )

    if(NOT gh_result EQUAL 0)
        file(REMOVE "${archive}")
        string(STRIP "${gh_error}" gh_error)
        set(${out_error} "can't download ${url} (${status_message}), and `gh release download` failed: ${gh_error}" PARENT_SCOPE)
    endif()
endfunction()

# Script mode: `cmake -DVERSION=... -DDESTINATION=... -P <this file>`.
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    if(NOT VERSION OR NOT DESTINATION)
        message(FATAL_ERROR "Usage: cmake -DVERSION=<tag|latest> -DDESTINATION=<dir> -P ${CMAKE_CURRENT_LIST_FILE}")
    endif()
    get_filename_component(destination "${DESTINATION}" ABSOLUTE)
    xboxrecomp2_download("${VERSION}" "${destination}" executable error)
    if(error)
        message(FATAL_ERROR "XboxRecomp2 ${VERSION}: ${error}.")
    endif()
    message("${executable}")
endif()
