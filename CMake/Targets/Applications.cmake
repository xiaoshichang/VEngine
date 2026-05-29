include_guard(GLOBAL)

function(ve_add_windows_player)
    if(NOT VE_BUILD_PLAYER)
        return()
    endif()

    if(WIN32)
        add_executable(VEnginePlayer WIN32
            Player/Windows/WindowsPlayer.cpp
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
        ve_add_imgui()

        add_executable(VEngineEditor WIN32
            Editor/Core/EditorProject.cpp
            Editor/Core/EditorProject.h
            Editor/Core/EditorReflection.cpp
            Editor/Core/EditorReflection.h
            Editor/Windows/WindowsEditor.cpp
            Editor/Windows/WindowsEditorAssetBrowserPanel.cpp
            Editor/Windows/WindowsEditorGameViewPanel.cpp
            Editor/Windows/WindowsEditorInspectorPanel.cpp
            Editor/Windows/WindowsEditorPanels.cpp
            Editor/Windows/WindowsEditorPanels.h
            Editor/Windows/WindowsProjectLauncher.cpp
            Editor/Windows/WindowsProjectLauncher.h
            Editor/Windows/WindowsEditorSceneHierarchyPanel.cpp
            Editor/Windows/WindowsEditorSceneViewPanel.cpp
            Tools/AssetTool/ObjImporter.cpp
            Tools/AssetTool/ObjImporter.h
            Tools/Package/PackageService.cpp
            Tools/Package/PackageService.h
        )

        target_link_libraries(VEngineEditor
            PRIVATE
                VEngine
                VEngine::ImGui
                advapi32
                gdi32
                ole32
                shell32
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
