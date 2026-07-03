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
    Editor/Core/EditorProjectPacker.cpp
    Editor/Core/EditorProjectPacker.h
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
    Editor/Core/EditorToolchain.cpp
    Editor/Core/EditorToolchain.h
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

function(ve_copy_windows_scripting_payload target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:${target_name}>/Managed/VEngine.ScriptHost"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}"
            "$<TARGET_FILE_DIR:${target_name}>/Managed/VEngine.ScriptHost"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:${target_name}>/DotNet"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VE_DOTNET_RUNTIME_ROOT}"
            "$<TARGET_FILE_DIR:${target_name}>/DotNet/win-x64/10.0.9"
        COMMENT "Copying app-local .NET scripting payload"
    )
endfunction()

function(ve_add_editor_packaging_definitions target_name)
    target_compile_definitions(${target_name}
        PRIVATE
            VE_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
            VE_CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
            VE_CMAKE_BUILD_CONFIG="$<CONFIG>"
    )
endfunction()

function(ve_copy_mac_scripting_payload target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:${target_name}>/Frameworks/DotNet"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:${target_name}>/../../../${target_name}.Managed"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:${target_name}>/Resources/Managed/VEngine.ScriptHost"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}"
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

    ve_copy_windows_scripting_payload(VEngineWinPlayer)

    ve_configure_target(VEngineWinPlayer)
endfunction()

function(ve_add_windows_editor)
    add_executable(VEngineWinEditor WIN32
        ${VE_EDITOR_COMMON_SOURCES}
        Editor/Windows/main.cpp
        Editor/Windows/WinEditorInputBackend.cpp
        Editor/Windows/WinEditorInputBackend.h
        Editor/Windows/WinEditorProjectPacker.cpp
        Editor/Windows/WinEditorProjectPacker.h
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
    ve_add_editor_packaging_definitions(VEngineWinEditor)

    add_custom_command(TARGET VEngineWinEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/Assets"
            "$<TARGET_FILE_DIR:VEngineWinEditor>/Assets"
        COMMENT "Copying VEngine editor asset roots"
    )

    ve_copy_windows_scripting_payload(VEngineWinEditor)

    ve_configure_target(VEngineWinEditor)
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
    find_library(GAMECONTROLLER_FRAMEWORK GameController REQUIRED)

    target_link_libraries(VEngineMacPlayer
        PRIVATE
            ${FOUNDATION_FRAMEWORK}
            ${APPKIT_FRAMEWORK}
            ${GAMECONTROLLER_FRAMEWORK}
            VEngine::ImGui
    )

    ve_setup_imgui(VEngineMacPlayer)

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
    find_library(GAMECONTROLLER_FRAMEWORK GameController REQUIRED)

    target_link_libraries(VEngineMacEditor
        PRIVATE
            ${FOUNDATION_FRAMEWORK}
            ${APPKIT_FRAMEWORK}
            ${GAMECONTROLLER_FRAMEWORK}
            VEngine::ImGui
    )

    ve_add_shader_tool()
    ve_add_managed_script_host()
    add_dependencies(VEngineMacEditor VEngineShaderTool VEngineScriptHostManaged)

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

function(ve_add_ios_player)
    if(NOT VE_BUILD_IOS_PLAYER)
        return()
    endif()

    enable_language(OBJCXX)

    add_executable(VEngineIOSPlayer MACOSX_BUNDLE
        Player/iOS/IOSPlayer.mm
        Player/iOS/LaunchScreen.storyboard
    )

    target_link_libraries(VEngineIOSPlayer
        PRIVATE
            VEngine
            "-framework Foundation"
            "-framework UIKit"
            "-framework Metal"
            "-framework QuartzCore"
    )

    if(VE_IOS_NATIVEAOT_LIBRARY)
        if(NOT EXISTS "${VE_IOS_NATIVEAOT_LIBRARY}")
            message(FATAL_ERROR "VE_IOS_NATIVEAOT_LIBRARY does not exist: ${VE_IOS_NATIVEAOT_LIBRARY}")
        endif()

        target_link_libraries(VEngineIOSPlayer
            PRIVATE
                "${VE_IOS_NATIVEAOT_LIBRARY}"
        )

        target_link_options(VEngineIOSPlayer
            PRIVATE
                "-Wl,-force_load,${VE_IOS_NATIVEAOT_LIBRARY}"
        )

        if(NOT VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR)
            message(FATAL_ERROR "VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR must point at the iOS NativeAOT runtime native directory when VE_IOS_NATIVEAOT_LIBRARY is set.")
        endif()

        if(NOT EXISTS "${VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR}")
            message(FATAL_ERROR "VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR does not exist: ${VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR}")
        endif()

        set(ve_ios_nativeaot_runtime_inputs
            libSystem.Native.a
            libSystem.IO.Compression.Native.a
            libSystem.Net.Security.Native.a
            libSystem.Security.Cryptography.Native.Apple.a
            libbootstrapperdll.o
            libRuntime.WorkstationGC.a
            libeventpipe-disabled.a
            libstandalonegc-disabled.a
            libaotminipal.a
            libstdc++compat.a
            libbrotlienc.a
            libbrotlidec.a
            libbrotlicommon.a
        )

        foreach(ve_ios_nativeaot_runtime_input IN LISTS ve_ios_nativeaot_runtime_inputs)
            set(ve_ios_nativeaot_runtime_path "${VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR}/${ve_ios_nativeaot_runtime_input}")
            if(NOT EXISTS "${ve_ios_nativeaot_runtime_path}")
                message(FATAL_ERROR "Required iOS NativeAOT runtime input does not exist: ${ve_ios_nativeaot_runtime_path}")
            endif()

            target_link_libraries(VEngineIOSPlayer
                PRIVATE
                    "${ve_ios_nativeaot_runtime_path}"
            )
        endforeach()

        target_link_libraries(VEngineIOSPlayer
            PRIVATE
                "-framework CoreFoundation"
                "-framework CryptoKit"
                "-framework Network"
                "-framework Security"
                "-framework GSS"
                "-L/usr/lib/swift"
                "-lswiftCore"
                "-lswiftFoundation"
                dl
                objc
                z
                icucore
                m
        )
    endif()

    set_source_files_properties(Player/iOS/LaunchScreen.storyboard
        PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources
    )
    set_source_files_properties(Player/iOS/IOSPlayer.mm
        PROPERTIES
            COMPILE_OPTIONS "-fobjc-arc"
    )

    set_target_properties(VEngineIOSPlayer
        PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/Player/iOS/Info.plist.in
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${VE_IOS_BUNDLE_IDENTIFIER}"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${VE_IOS_DEVELOPMENT_TEAM}"
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "${VE_IOS_CODE_SIGN_STYLE}"
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${VE_IOS_DEPLOYMENT_TARGET}"
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator"
            XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    )

    if(VE_IOS_PROVISIONING_PROFILE_SPECIFIER)
        set_target_properties(VEngineIOSPlayer
            PROPERTIES
                XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "${VE_IOS_PROVISIONING_PROFILE_SPECIFIER}"
        )
    endif()

    if(VE_IOS_CODE_SIGN_IDENTITY)
        set_target_properties(VEngineIOSPlayer
            PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${VE_IOS_CODE_SIGN_IDENTITY}"
        )
    endif()

    if(VE_IOS_PACKAGE_DATA_ROOT)
        add_custom_command(TARGET VEngineIOSPlayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_BUNDLE_DIR:VEngineIOSPlayer>/Data"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${VE_IOS_PACKAGE_DATA_ROOT}" "$<TARGET_BUNDLE_DIR:VEngineIOSPlayer>/Data"
            COMMENT "Copying packaged iOS runtime data"
        )
    endif()

    ve_configure_target(VEngineIOSPlayer)
endfunction()

function(ve_add_player)
    if(NOT VE_BUILD_PLAYER)
        return()
    endif()

    if(WIN32)
        ve_add_windows_player()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        ve_add_ios_player()
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
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        return()
    elseif(APPLE)
        ve_add_mac_editor()
    endif()
endfunction()
