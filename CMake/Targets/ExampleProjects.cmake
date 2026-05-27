include_guard(GLOBAL)

function(ve_add_example_projects)
    if(NOT VE_BUILD_EXAMPLE_PROJECTS)
        return()
    endif()

    if(NOT WIN32)
        return()
    endif()

    ve_add_asset_tool()

    set(veAssetPipelineSampleRoot "${PROJECT_SOURCE_DIR}/Examples/AssetPipelineSample")

    add_custom_target(VEngineExampleAssetPipelineSample ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${veAssetPipelineSampleRoot}/Generated/Assets/ImportCache"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${veAssetPipelineSampleRoot}/Generated/Build/Windows"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${veAssetPipelineSampleRoot}/Generated/Editor/Workspace"
        COMMAND $<TARGET_FILE:VEngineAssetTool>
            import
            --project "${veAssetPipelineSampleRoot}"
            --source "Assets/Samples/Models/Cube.obj"
        COMMAND $<TARGET_FILE:VEngineAssetTool>
            validate
            --project "${veAssetPipelineSampleRoot}"
        DEPENDS VEngineAssetTool
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Building VEngine example project: AssetPipelineSample"
        VERBATIM
    )

    set_target_properties(VEngineExampleAssetPipelineSample
        PROPERTIES
            FOLDER "Examples"
    )

    if(TARGET VEnginePlayer)
        add_dependencies(VEnginePlayer VEngineExampleAssetPipelineSample)

        ve_add_package_tool()

        add_custom_target(VEngineExampleAssetPipelineSamplePackage ALL
            COMMAND $<TARGET_FILE:VEnginePackageTool>
                package
                --project "${veAssetPipelineSampleRoot}"
                --platform Windows
                --config $<CONFIG>
                --output "${veAssetPipelineSampleRoot}/Generated/Build/Windows/$<CONFIG>"
                --player $<TARGET_FILE:VEnginePlayer>
            DEPENDS
                VEnginePackageTool
                VEnginePlayer
                VEngineExampleAssetPipelineSample
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Packaging VEngine example project: AssetPipelineSample"
            VERBATIM
        )

        set_target_properties(VEngineExampleAssetPipelineSamplePackage
            PROPERTIES
                FOLDER "Examples"
        )
    endif()

    if(TARGET VEngineEditor)
        add_dependencies(VEngineEditor VEngineExampleAssetPipelineSample)
    endif()
endfunction()
