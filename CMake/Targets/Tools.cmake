include_guard(GLOBAL)

function(ve_add_shader_tool)
    if(TARGET VEngineShaderTool)
        return()
    endif()

    ve_setup_directx_shader_compiler()
    ve_setup_slang()

    add_executable(VEngineShaderTool
        Tools/ShaderTool/ShaderTool.cpp
    )

    if(VE_SLANG_EXECUTABLE)
        file(TO_CMAKE_PATH "${VE_SLANG_EXECUTABLE}" veSlangExecutablePath)
        target_compile_definitions(VEngineShaderTool
            PRIVATE
                VE_DEFAULT_SLANG_EXECUTABLE="${veSlangExecutablePath}"
        )
    endif()

    target_link_libraries(VEngineShaderTool
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineShaderTool)
endfunction()

function(ve_add_tools)
    if(NOT VE_BUILD_TOOLS)
        return()
    endif()

    add_executable(VEngineAssetTool
        Tools/AssetTool/AssetTool.cpp
    )

    target_link_libraries(VEngineAssetTool
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineAssetTool)

    ve_add_shader_tool()
endfunction()
