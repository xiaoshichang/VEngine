include_guard(GLOBAL)

function(ve_add_windows_player)
    if(NOT VE_BUILD_PLAYER)
        return()
    endif()

    if(WIN32)
        add_executable(VEnginePlayer WIN32
            Player/Windows/main.cpp
            Player/Windows/WindowsPlayer.cpp
            Player/Windows/WindowsPlayer.h
        )

        target_link_libraries(VEnginePlayer
            PRIVATE
                VEngine
        )

        ve_configure_target(VEnginePlayer)
    else()
        message(STATUS "VEnginePlayer is only built on Windows.")
    endif()
endfunction()

function(ve_add_windows_editor)
    if(NOT VE_BUILD_EDITOR)
        return()
    endif()

    if(WIN32)
        add_executable(VEngineEditor WIN32
            Editor/Core/EditorAssetDatabase.cpp
            Editor/Core/EditorAssetDatabase.h
            Editor/Core/EditorAssetPath.cpp
            Editor/Core/EditorAssetPath.h
            Editor/Core/EditorBuildPackageDialog.cpp
            Editor/Core/EditorBuildPackageDialog.h
            Editor/Core/EditorBuiltinResources.cpp
            Editor/Core/EditorBuiltinResources.h
            Editor/Core/Editor.cpp
            Editor/Core/Editor.h
            Editor/Core/EditorEventDispatcher.h
            Editor/Core/EditorEvents.h
            Editor/Core/EditorInput.cpp
            Editor/Core/EditorInput.h
            Editor/Core/EditorProject.cpp
            Editor/Core/EditorProject.h
            Editor/Core/EditorProjectDirectoryDialog.cpp
            Editor/Core/EditorProjectDirectoryDialog.h
            Editor/Core/EditorProjectEditingView.cpp
            Editor/Core/EditorProjectEditingView.h
            Editor/Core/EditorProjectPackager.cpp
            Editor/Core/EditorProjectPackager.h
            Editor/Core/EditorProjectRegistry.cpp
            Editor/Core/EditorProjectRegistry.h
            Editor/Core/EditorProjectSelectionView.cpp
            Editor/Core/EditorProjectSelectionView.h
            "../../Editor/Core/EditorResourceLoader.cpp"
            "../../Editor/Core/EditorResourceLoader.h"
            Editor/Core/Gizmos.cpp
            Editor/Core/Gizmos.h
            Editor/RenderPass/EditorGizmoRenderPass.cpp
            Editor/RenderPass/EditorGizmoRenderPass.h
            Editor/RenderPass/SceneGridRenderPass.cpp
            Editor/RenderPass/SceneGridRenderPass.h
            Editor/Panels/AssetsPanel.cpp
            Editor/Panels/AssetsPanel.h
            Editor/Panels/BasePanel.cpp
            Editor/Panels/BasePanel.h
            Editor/Panels/GameViewPanel.cpp
            Editor/Panels/GameViewPanel.h
            Editor/Panels/HierarchyPanel.cpp
            Editor/Panels/HierarchyPanel.h
            Editor/Panels/InspectorPanel.cpp
            Editor/Panels/InspectorPanel.h
            Editor/Panels/SceneViewPanel.cpp
            Editor/Panels/SceneViewPanel.h
            Editor/Windows/main.cpp
            Editor/Windows/WindowsEditorApplication.cpp
            Editor/Windows/WindowsEditorApplication.h
        )

        target_link_libraries(VEngineEditor
            PRIVATE
                VEngine
        )

        ve_add_shader_tool()
        add_dependencies(VEngineEditor VEngineShaderTool)

        ve_setup_imgui(VEngineEditor)

        add_custom_command(TARGET VEngineEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PROJECT_SOURCE_DIR}/BuiltinAsset"
                "$<TARGET_FILE_DIR:VEngineEditor>/BuiltinAsset"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PROJECT_SOURCE_DIR}/EditorOnlyAsset"
                "$<TARGET_FILE_DIR:VEngineEditor>/EditorOnlyAsset"
            COMMENT "Copying VEngine editor asset roots"
        )

        ve_configure_target(VEngineEditor)
    else()
        message(STATUS "VEngineEditor is only built on Windows.")
    endif()
endfunction()

function(ve_add_ios_player)
    if(NOT VE_BUILD_IOS_PLAYER)
        return()
    endif()

    if(NOT APPLE)
        message(FATAL_ERROR "VEngineIOSPlayer requires an Apple toolchain.")
    endif()

    enable_language(OBJCXX)

    add_executable(VEngineIOSPlayer MACOSX_BUNDLE
        Player/iOS/IOSPlayer.mm
    )

    target_link_libraries(VEngineIOSPlayer
        PRIVATE
            VEngine
    )

    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(UIKIT_FRAMEWORK UIKit REQUIRED)

    target_link_libraries(VEngineIOSPlayer
        PRIVATE
            ${FOUNDATION_FRAMEWORK}
            ${UIKIT_FRAMEWORK}
    )

    set_target_properties(VEngineIOSPlayer
        PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/Player/iOS/Info.plist.in
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${VE_IOS_BUNDLE_IDENTIFIER}
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphonesimulator iphoneos"
    )

    ve_configure_target(VEngineIOSPlayer)
endfunction()
