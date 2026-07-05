include_guard(GLOBAL)

function(ve_add_shader_tool)
    if(TARGET VEngineShaderTool)
        return()
    endif()

    ve_setup_slang()

    if(WIN32)
        ve_setup_directx_shader_compiler()
    endif()

    add_executable(VEngineShaderTool
        Tools/ShaderTool/ShaderTool.cpp
    )

    target_link_libraries(VEngineShaderTool
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineShaderTool)
endfunction()
