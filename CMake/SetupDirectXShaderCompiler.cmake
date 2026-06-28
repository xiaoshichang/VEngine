include_guard(GLOBAL)

get_filename_component(_VE_DXC_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(VE_DXC_EXECUTABLE "" CACHE FILEPATH "Path to the dxc executable used for DXIL output.")
set(VE_FXC_EXECUTABLE "" CACHE FILEPATH "Path to the fxc executable used for D3D11 DXBC output.")
set(VE_DXC_NUGET_VERSION "1.9.2602.17" CACHE STRING "Microsoft.Direct3D.DXC NuGet package version.")
set(VE_DXC_THIRD_PARTY_ROOT "${_VE_DXC_REPOSITORY_ROOT}/ThirdParty/DirectXShaderCompiler" CACHE PATH "DirectXShaderCompiler third-party root.")

function(ve_reset_old_build_local_dxc_cache)
    if(NOT VE_DXC_EXECUTABLE)
        return()
    endif()

    if(NOT EXISTS "${VE_DXC_EXECUTABLE}")
        set(VE_DXC_EXECUTABLE "" CACHE FILEPATH "Path to the dxc executable used for DXIL output." FORCE)
        return()
    endif()

    file(TO_CMAKE_PATH "${VE_DXC_EXECUTABLE}" normalizedDxcExecutable)
    file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/_deps/directx-dxc-" oldBuildDependencyPrefix)
    string(FIND "${normalizedDxcExecutable}" "${oldBuildDependencyPrefix}" oldBuildDependencyIndex)

    if(oldBuildDependencyIndex EQUAL 0)
        set(VE_DXC_EXECUTABLE "" CACHE FILEPATH "Path to the dxc executable used for DXIL output." FORCE)
    endif()
endfunction()

function(ve_setup_directx_shader_compiler)
    ve_reset_old_build_local_dxc_cache()

    set(windowsSdkToolHints "")
    set(defaultDxcExecutable "${VE_DXC_THIRD_PARTY_ROOT}/Build/Windows64/${VE_DXC_NUGET_VERSION}/Tools/x64/dxc.exe")

    if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKVersion})
        list(APPEND windowsSdkToolHints "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64")
    endif()

    if(DEFINED ENV{WindowsSdkVerBinPath})
        list(APPEND windowsSdkToolHints "$ENV{WindowsSdkVerBinPath}/x64")
    endif()

    if(NOT VE_DXC_EXECUTABLE AND EXISTS "${defaultDxcExecutable}")
        set(VE_DXC_EXECUTABLE "${defaultDxcExecutable}" CACHE FILEPATH "Path to the dxc executable used for DXIL output." FORCE)
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
            "dxc was not found. Set VE_DXC_EXECUTABLE to a DXIL-capable dxc executable "
            "or run ThirdParty/DirectXShaderCompiler/Build_Windows64.bat before configuring."
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
