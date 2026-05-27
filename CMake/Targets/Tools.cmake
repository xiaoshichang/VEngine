include_guard(GLOBAL)

function(ve_add_shader_tool)
    if(TARGET VEngineShaderTool)
        return()
    endif()

    ve_setup_directx_shader_compiler()
    ve_setup_spirv_cross()

    add_executable(VEngineShaderTool
        Tools/ShaderTool/ShaderTool.cpp
    )

    target_link_libraries(VEngineShaderTool
        PRIVATE
            VEngine
    )

    if(VE_SPIRV_CROSS_TARGET)
        add_dependencies(VEngineShaderTool ${VE_SPIRV_CROSS_TARGET})
    endif()

    ve_configure_target(VEngineShaderTool)
endfunction()

function(ve_add_asset_tool)
    if(TARGET VEngineAssetTool)
        return()
    endif()

    add_executable(VEngineAssetTool
        Tools/AssetTool/AssetTool.cpp
        Tools/AssetTool/ObjImporter.cpp
        Tools/AssetTool/ObjImporter.h
    )

    target_link_libraries(VEngineAssetTool
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineAssetTool)
endfunction()

function(ve_add_package_tool)
    if(TARGET VEnginePackageTool)
        return()
    endif()

    add_executable(VEnginePackageTool
        Tools/Package/PackageService.cpp
        Tools/Package/PackageService.h
        Tools/Package/PackageTool.cpp
    )

    target_link_libraries(VEnginePackageTool
        PRIVATE
            VEngine
    )

    ve_configure_target(VEnginePackageTool)
endfunction()

function(ve_add_tools)
    if(NOT VE_BUILD_TOOLS)
        return()
    endif()

    ve_add_asset_tool()
    ve_add_package_tool()
    ve_add_shader_tool()
endfunction()
