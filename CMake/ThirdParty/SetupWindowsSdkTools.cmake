include_guard(GLOBAL)

get_filename_component(_VE_WINDOWS_SDK_TOOLS_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(VE_WINDOWS_SDK_TOOLS_THIRD_PARTY_ROOT "${_VE_WINDOWS_SDK_TOOLS_REPOSITORY_ROOT}/ThirdParty/WindowsSdkTools" CACHE PATH "Windows SDK tools third-party root.")
set(VE_FXC_EXECUTABLE "" CACHE FILEPATH "Path to the fxc executable used for D3D11 DXBC output.")

function(ve_setup_windows_sdk_tools)
    if(NOT WIN32)
        return()
    endif()

    if(VE_FXC_EXECUTABLE AND EXISTS "${VE_FXC_EXECUTABLE}")
        ve_add_third_party_marker_target(VEngineThirdPartyWindowsSdkTools)
        message(STATUS "FXC executable: ${VE_FXC_EXECUTABLE}")
        return()
    endif()

    set(defaultFxcExecutable "${VE_WINDOWS_SDK_TOOLS_THIRD_PARTY_ROOT}/Tools/x64/fxc.exe")
    if(NOT EXISTS "${defaultFxcExecutable}" AND COMMAND ve_run_third_party_setup)
        ve_run_third_party_setup(windowssdktools)
    endif()

    if(EXISTS "${defaultFxcExecutable}")
        set(VE_FXC_EXECUTABLE "${defaultFxcExecutable}" CACHE FILEPATH "Path to the fxc executable used for D3D11 DXBC output." FORCE)
        ve_add_third_party_marker_target(VEngineThirdPartyWindowsSdkTools)
        message(STATUS "FXC executable: ${VE_FXC_EXECUTABLE}")
        return()
    endif()

    message(FATAL_ERROR
        "fxc was not found. Run CMake through CMake/Scripts/WithMsvc.bat or prepare ThirdParty/WindowsSdkTools first."
    )
endfunction()
