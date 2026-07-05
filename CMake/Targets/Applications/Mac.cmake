include_guard(GLOBAL)

function(ve_copy_mac_scripting_payload target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:${target_name}>/Frameworks/DotNet"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:${target_name}>/../../../${target_name}.Managed"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:${target_name}>/Resources/Managed/VEngine.ScriptHost"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VE_ENGINE_SCRIPT_HOST_OUTPUT_DIR}"
            "$<TARGET_BUNDLE_CONTENT_DIR:${target_name}>/Resources/Managed/VEngine.ScriptHost"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:${target_name}>/Resources/DotNet"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VE_DOTNET_RUNTIME_ROOT}"
            "$<TARGET_BUNDLE_CONTENT_DIR:${target_name}>/Resources/DotNet/osx-arm64/10.0.9"
        COMMAND /bin/sh
            "${PROJECT_SOURCE_DIR}/CMake/Scripts/CodeSignMacBundleAfterPostBuild.sh"
            "$<TARGET_BUNDLE_DIR:${target_name}>"
        COMMENT "Copying bundled .NET scripting payload"
    )
endfunction()

function(ve_add_mac_player)
    enable_language(OBJCXX)

    add_executable(VEngineMacPlayer
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

    target_link_libraries(VEngineMacPlayer
        PRIVATE
            ${FOUNDATION_FRAMEWORK}
            ${APPKIT_FRAMEWORK}
    )

    set_target_properties(VEngineMacPlayer
        PROPERTIES
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
        Editor/macOS/MacEditorProjectPacker.cpp
        Editor/macOS/MacEditorProjectPacker.h
        Editor/macOS/IOSEditorProjectPacker.cpp
        Editor/macOS/IOSEditorProjectPacker.h
        Editor/macOS/MacEditorProjectRegistryBackend.h
        Editor/macOS/MacEditorProjectRegistryBackend.mm
        Editor/macOS/MacEditorRenderBackend.h
        Editor/macOS/MacEditorRenderBackend.mm
        Editor/macOS/MacEditorWindowPlacement.h
        Editor/macOS/MacEditorWindowPlacement.mm
        Editor/macOS/main.mm
    )

    target_link_libraries(VEngineMacEditor
        PRIVATE
            VEngine
    )

    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)

    target_link_libraries(VEngineMacEditor
        PRIVATE
            ${FOUNDATION_FRAMEWORK}
            ${APPKIT_FRAMEWORK}
            VEngine::ImGui
    )

    ve_add_shader_tool()
    ve_add_engine_script_host()
    add_dependencies(VEngineMacEditor VEngineShaderTool VEngineEngineScriptHost)

    ve_setup_imgui(VEngineMacEditor)
    ve_add_editor_packaging_definitions(VEngineMacEditor)

    ve_copy_mac_scripting_payload(VEngineMacEditor)

    set_target_properties(VEngineMacEditor
        PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/Editor/macOS/Info.plist.in
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${VE_MAC_BUNDLE_IDENTIFIER}.editor"
            XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "macosx"
            XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "YES"
    )

    ve_configure_target(VEngineMacEditor)
endfunction()
