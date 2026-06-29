include_guard(GLOBAL)

set(VE_EDITOR_COMMON_SOURCES
    Editor/Core/Editor.cpp
    Editor/Core/Editor.h
    Editor/Core/EditorAssetDatabase.cpp
    Editor/Core/EditorAssetDatabase.h
    Editor/Core/EditorAssetPath.cpp
    Editor/Core/EditorAssetPath.h
    Editor/Core/EditorBuildPackageDialog.cpp
    Editor/Core/EditorBuildPackageDialog.h
    Editor/Core/EditorBuiltinResources.cpp
    Editor/Core/EditorBuiltinResources.h
    Editor/Core/EditorEventDispatcher.h
    Editor/Core/EditorEvents.h
    Editor/Core/EditorInput.cpp
    Editor/Core/EditorInput.h
    Editor/Core/EditorInputBackend.h
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
    Editor/Core/EditorProjectRegistryBackend.h
    Editor/Core/EditorProjectSelectionView.cpp
    Editor/Core/EditorProjectSelectionView.h
    Editor/Core/EditorRenderBackend.h
    Editor/Core/EditorResourceLoader.cpp
    Editor/Core/EditorResourceLoader.h
    Editor/Core/EditorScriptCompiler.cpp
    Editor/Core/EditorScriptCompiler.h
    Editor/Core/EditorScriptDatabase.cpp
    Editor/Core/EditorScriptDatabase.h
    Editor/Core/EditorScriptProjectGenerator.cpp
    Editor/Core/EditorScriptProjectGenerator.h
    Editor/Core/Gizmos.cpp
    Editor/Core/Gizmos.h
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
    Editor/RenderPass/EditorGizmoRenderPass.cpp
    Editor/RenderPass/EditorGizmoRenderPass.h
    Editor/RenderPass/SceneGridRenderPass.cpp
    Editor/RenderPass/SceneGridRenderPass.h
)

function(ve_add_windows_player)
    add_executable(VEngineWinPlayer WIN32
        Player/Windows/main.cpp
        Player/Windows/VEnginePlayer.cpp
        Player/Windows/VEnginePlayer.h
    )

    target_link_libraries(VEngineWinPlayer
        PRIVATE
            VEngine
    )

    ve_add_managed_script_host()
    add_dependencies(VEngineWinPlayer VEngineScriptHostManaged)

    add_custom_command(TARGET VEngineWinPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}"
            "$<TARGET_FILE_DIR:VEngineWinPlayer>/Managed/VEngine.ScriptHost"
        COMMENT "Copying VEngine.ScriptHost managed assembly"
    )

    ve_configure_target(VEngineWinPlayer)
endfunction()

function(ve_add_windows_editor)
    add_executable(VEngineWinEditor WIN32
        ${VE_EDITOR_COMMON_SOURCES}
        Editor/Windows/main.cpp
        Editor/Windows/WinEditorInputBackend.cpp
        Editor/Windows/WinEditorInputBackend.h
        Editor/Windows/WinEditorProjectRegistryBackend.cpp
        Editor/Windows/WinEditorProjectRegistryBackend.h
        Editor/Windows/WinEditorRenderBackend.cpp
        Editor/Windows/WinEditorRenderBackend.h
        Editor/Windows/WindowsEditorApplication.cpp
        Editor/Windows/WindowsEditorApplication.h
    )

    target_link_libraries(VEngineWinEditor
        PRIVATE
            VEngine
    )

    ve_add_shader_tool()
    ve_add_managed_script_host()
    add_dependencies(VEngineWinEditor VEngineShaderTool VEngineScriptHostManaged)

    ve_setup_imgui(VEngineWinEditor)

    add_custom_command(TARGET VEngineWinEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/Assets"
            "$<TARGET_FILE_DIR:VEngineWinEditor>/Assets"
        COMMENT "Copying VEngine editor asset roots"
    )

    add_custom_command(TARGET VEngineWinEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}"
            "$<TARGET_FILE_DIR:VEngineWinEditor>/Managed/VEngine.ScriptHost"
        COMMENT "Copying VEngine.ScriptHost managed assembly"
    )

    ve_configure_target(VEngineWinEditor)
endfunction()

function(ve_add_mac_player)
    enable_language(OBJCXX)

    add_executable(VEngineMacPlayer MACOSX_BUNDLE
        Player/Windows/VEnginePlayer.cpp
        Player/Windows/VEnginePlayer.h
        Player/macOS/MacPlayer.mm
    )

    target_link_libraries(VEngineMacPlayer
        PRIVATE
            VEngine
    )

    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(GAMECONTROLLER_FRAMEWORK GameController REQUIRED)

    target_link_libraries(VEngineMacPlayer
        PRIVATE
            ${FOUNDATION_FRAMEWORK}
            ${APPKIT_FRAMEWORK}
            ${GAMECONTROLLER_FRAMEWORK}
            VEngine::ImGui
    )

    ve_add_managed_script_host()
    add_dependencies(VEngineMacPlayer VEngineScriptHostManaged)

    ve_setup_imgui(VEngineMacPlayer)

    add_custom_command(TARGET VEngineMacPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:VEngineMacPlayer>/../../VEngineMacPlayer.Managed"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:VEngineMacPlayer>/../VEngineMacPlayer.Managed"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}"
            "$<TARGET_FILE_DIR:VEngineMacPlayer>/../../../VEngineMacPlayer.Managed/VEngine.ScriptHost"
        COMMENT "Copying VEngine.ScriptHost managed assembly"
    )

    set_target_properties(VEngineMacPlayer
        PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/Player/macOS/Info.plist.in
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${VE_MAC_BUNDLE_IDENTIFIER}
            XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "macosx"
            XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "YES"
    )

    ve_configure_target(VEngineMacPlayer)
endfunction()

function(ve_add_mac_editor)
    enable_language(OBJCXX)

    add_executable(VEngineMacEditor MACOSX_BUNDLE
        ${VE_EDITOR_COMMON_SOURCES}
        Editor/macOS/MacEditorApplication.cpp
        Editor/macOS/MacEditorApplication.h
        Editor/macOS/MacEditorInputBackend.h
        Editor/macOS/MacEditorInputBackend.mm
        Editor/macOS/MacEditorProjectRegistryBackend.h
        Editor/macOS/MacEditorProjectRegistryBackend.mm
        Editor/macOS/MacEditorRenderBackend.h
        Editor/macOS/MacEditorRenderBackend.mm
        Editor/macOS/main.mm
    )

    target_link_libraries(VEngineMacEditor
        PRIVATE
            VEngine
    )

    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(GAMECONTROLLER_FRAMEWORK GameController REQUIRED)

    target_link_libraries(VEngineMacEditor
        PRIVATE
            ${FOUNDATION_FRAMEWORK}
            ${APPKIT_FRAMEWORK}
            ${GAMECONTROLLER_FRAMEWORK}
            VEngine::ImGui
    )

    ve_add_managed_script_host()
    add_dependencies(VEngineMacEditor VEngineScriptHostManaged)

    ve_setup_imgui(VEngineMacEditor)

    add_custom_command(TARGET VEngineMacEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove -f
            "$<TARGET_FILE_DIR:VEngineMacEditor>/imgui.ini"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:VEngineMacEditor>/../../VEngineMacEditor.Managed"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:VEngineMacEditor>/../VEngineMacEditor.Managed"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}"
            "$<TARGET_FILE_DIR:VEngineMacEditor>/../../../VEngineMacEditor.Managed/VEngine.ScriptHost"
        COMMENT "Copying VEngine.ScriptHost managed assembly"
    )

    set_target_properties(VEngineMacEditor
        PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/Editor/macOS/Info.plist.in
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${VE_MAC_BUNDLE_IDENTIFIER}.editor"
            XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "macosx"
            XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "YES"
    )

    ve_configure_target(VEngineMacEditor)
endfunction()

function(ve_add_player)
    if(NOT VE_BUILD_PLAYER)
        return()
    endif()

    if(WIN32)
        ve_add_windows_player()
    elseif(APPLE)
        ve_add_mac_player()
    endif()
endfunction()

function(ve_add_editor)
    if(NOT VE_BUILD_EDITOR)
        return()
    endif()

    if(WIN32)
        ve_add_windows_editor()
    elseif(APPLE)
        ve_add_mac_editor()
    endif()
endfunction()
