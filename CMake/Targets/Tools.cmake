include_guard(GLOBAL)

include(CMake/Targets/Tools/AssetTool.cmake)
include(CMake/Targets/Tools/ShaderTool.cmake)

function(ve_add_tools)
    if(NOT VE_BUILD_TOOLS)
        return()
    endif()

    ve_add_asset_tool()
    ve_add_shader_tool()
endfunction()
