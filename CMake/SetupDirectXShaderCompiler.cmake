include_guard(GLOBAL)

get_filename_component(_VE_DXC_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(VE_DXC_EXECUTABLE "" CACHE FILEPATH "Path to a SPIR-V capable dxc executable.")
set(VE_FXC_EXECUTABLE "" CACHE FILEPATH "Path to the fxc executable used for D3D11 DXBC output.")
set(VE_DXC_NUGET_VERSION "1.9.2602.17" CACHE STRING "Microsoft.Direct3D.DXC NuGet package version.")
set(VE_DXC_NUGET_SHA256 "95703CA504F1C42B8FC3F0D1C4A7FED56BAE16299CF253D432BC90C24A86AE9A" CACHE STRING "Expected Microsoft.Direct3D.DXC NuGet SHA256.")
set(VE_DXC_THIRD_PARTY_ROOT "${_VE_DXC_REPOSITORY_ROOT}/ThirdParty/DirectXShaderCompiler" CACHE PATH "DirectXShaderCompiler third-party root.")
option(VE_DXC_DOWNLOAD_IF_MISSING "Download Microsoft.Direct3D.DXC into ThirdParty when VE_DXC_EXECUTABLE is not set." ON)

function(ve_reset_old_build_local_dxc_cache)
    if(NOT VE_DXC_EXECUTABLE)
        return()
    endif()

    if(NOT EXISTS "${VE_DXC_EXECUTABLE}")
        set(VE_DXC_EXECUTABLE "" CACHE FILEPATH "Path to a SPIR-V capable dxc executable." FORCE)
        return()
    endif()

    file(TO_CMAKE_PATH "${VE_DXC_EXECUTABLE}" normalizedDxcExecutable)
    file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/_deps/directx-dxc-" oldBuildDependencyPrefix)
    string(FIND "${normalizedDxcExecutable}" "${oldBuildDependencyPrefix}" oldBuildDependencyIndex)

    if(oldBuildDependencyIndex EQUAL 0)
        set(VE_DXC_EXECUTABLE "" CACHE FILEPATH "Path to a SPIR-V capable dxc executable." FORCE)
    endif()
endfunction()

function(ve_download_dxc_package outVariable)
    if(NOT WIN32)
        set(${outVariable} "" PARENT_SCOPE)
        return()
    endif()

    set(packageRoot "${VE_DXC_THIRD_PARTY_ROOT}/Build/Windows64/${VE_DXC_NUGET_VERSION}")
    set(dxcExecutable "${packageRoot}/Tools/x64/dxc.exe")

    if(NOT EXISTS "${dxcExecutable}")
        execute_process(
            COMMAND powershell
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${VE_DXC_THIRD_PARTY_ROOT}/Setup_Windows64.ps1"
                -Version "${VE_DXC_NUGET_VERSION}"
                -Sha256 "${VE_DXC_NUGET_SHA256}"
            WORKING_DIRECTORY "${_VE_DXC_REPOSITORY_ROOT}"
            RESULT_VARIABLE setupDxcResult
        )

        if(NOT setupDxcResult EQUAL 0)
            message(FATAL_ERROR "DirectXShaderCompiler setup failed with exit code ${setupDxcResult}.")
        endif()
    endif()

    if(EXISTS "${dxcExecutable}")
        set(${outVariable} "${dxcExecutable}" PARENT_SCOPE)
    else()
        set(${outVariable} "" PARENT_SCOPE)
    endif()
endfunction()

function(ve_setup_directx_shader_compiler)
    ve_reset_old_build_local_dxc_cache()

    set(windowsSdkToolHints "")

    if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKVersion})
        list(APPEND windowsSdkToolHints "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64")
    endif()

    if(DEFINED ENV{WindowsSdkVerBinPath})
        list(APPEND windowsSdkToolHints "$ENV{WindowsSdkVerBinPath}/x64")
    endif()

    if(NOT VE_DXC_EXECUTABLE AND VE_DXC_DOWNLOAD_IF_MISSING)
        ve_download_dxc_package(downloadedDxcExecutable)

        if(downloadedDxcExecutable)
            set(VE_DXC_EXECUTABLE "${downloadedDxcExecutable}" CACHE FILEPATH "Path to a SPIR-V capable dxc executable." FORCE)
        endif()
    endif()

    if(NOT VE_FXC_EXECUTABLE)
        find_program(foundFxcExecutable
            NAMES fxc fxc.exe
            HINTS ${windowsSdkToolHints}
            DOC "Path to the fxc executable used for D3D11 DXBC output."
        )

        if(foundFxcExecutable)
            set(VE_FXC_EXECUTABLE "${foundFxcExecutable}" CACHE FILEPATH "Path to the fxc executable used for D3D11 DXBC output." FORCE)
        endif()
    endif()

    if(NOT VE_DXC_EXECUTABLE)
        message(FATAL_ERROR
            "dxc was not found. Set VE_DXC_EXECUTABLE to a SPIR-V capable dxc executable "
            "or leave VE_DXC_DOWNLOAD_IF_MISSING enabled on Windows."
        )
    endif()

    if(WIN32 AND NOT VE_FXC_EXECUTABLE)
        message(FATAL_ERROR
            "fxc was not found. Run CMake through CMake/Scripts/WithMsvc.bat or set VE_FXC_EXECUTABLE."
        )
    endif()

    message(STATUS "DXC executable: ${VE_DXC_EXECUTABLE}")

    if(VE_FXC_EXECUTABLE)
        message(STATUS "FXC executable: ${VE_FXC_EXECUTABLE}")
    endif()
endfunction()
