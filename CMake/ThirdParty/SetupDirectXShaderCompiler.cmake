include_guard(GLOBAL)

get_filename_component(_VE_DXC_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(VE_DXC_EXECUTABLE "" CACHE FILEPATH "Path to the dxc executable used for DXIL output.")
set(VE_DXC_NUGET_VERSION "1.9.2602.17" CACHE STRING "Microsoft.Direct3D.DXC NuGet package version.")
set(VE_DXC_NUGET_SHA256 "95703CA504F1C42B8FC3F0D1C4A7FED56BAE16299CF253D432BC90C24A86AE9A" CACHE STRING "Microsoft.Direct3D.DXC NuGet package SHA-256.")
set(VE_DXC_THIRD_PARTY_ROOT "${_VE_DXC_REPOSITORY_ROOT}/ThirdParty/DirectXShaderCompiler" CACHE PATH "DirectXShaderCompiler third-party root.")

function(ve_setup_directx_shader_compiler)
    set(defaultDxcExecutable "${VE_DXC_THIRD_PARTY_ROOT}/Build/Windows64/${VE_DXC_NUGET_VERSION}/Tools/x64/dxc.exe")
    if(NOT EXISTS "${defaultDxcExecutable}" AND WIN32 AND COMMAND ve_run_third_party_setup)
        ve_run_third_party_setup(directxshadercompiler --version "${VE_DXC_NUGET_VERSION}" --sha256 "${VE_DXC_NUGET_SHA256}")
    endif()

    if(NOT EXISTS "${defaultDxcExecutable}")
        message(FATAL_ERROR
            "Project-local dxc was not found: ${defaultDxcExecutable}. CMake could not prepare DirectXShaderCompiler automatically."
        )
    endif()

    set(VE_DXC_EXECUTABLE "${defaultDxcExecutable}" CACHE FILEPATH "Path to the dxc executable used for DXIL output." FORCE)
    ve_add_third_party_marker_target(VEngineThirdPartyDirectXShaderCompiler)
    message(STATUS "DXC executable: ${VE_DXC_EXECUTABLE}")

    if(WIN32)
        ve_setup_windows_sdk_tools()
    endif()
endfunction()
