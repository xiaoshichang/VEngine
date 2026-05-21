cmake_minimum_required(VERSION 3.25)

project(VEngine
    VERSION 0.1.0
    DESCRIPTION "A lightweight cross-platform 3D mini game engine"
    LANGUAGES CXX
)

option(VE_BUILD_PLAYER "Build Windows player" ON)
option(VE_BUILD_EDITOR "Build Windows editor" ON)
option(VE_BUILD_TESTS "Build Windows tests" ON)
option(VE_BUILD_TOOLS "Build command line tools" ON)
option(VE_BUILD_IOS_PLAYER "Build iOS player app" OFF)
option(VE_BUILD_RHI_DEMOS "Build RHI triangle demo programs" ON)

option(VE_ENABLE_D3D11 "Enable D3D11 RHI" ON)
option(VE_ENABLE_D3D12 "Enable D3D12 RHI" ON)
option(VE_ENABLE_METAL "Enable Metal RHI" OFF)
option(VE_ENABLE_SCRIPTING "Enable C# scripting" ON)

set(VE_IOS_BUNDLE_IDENTIFIER "com.vengine.player" CACHE STRING "iOS player bundle identifier")

include(CMake/SetupBoostLibrary.cmake)

if(APPLE AND VE_ENABLE_METAL)
    enable_language(OBJCXX)
endif()

function(ve_configure_target targetName)
    set_target_properties(${targetName}
        PROPERTIES
            CXX_EXTENSIONS OFF
    )

    target_compile_features(${targetName}
        PRIVATE
            cxx_std_20
    )

    if(MSVC)
        target_compile_options(${targetName}
            PRIVATE
                /W4
                /permissive-
        )
    else()
        target_compile_options(${targetName}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
        )
    endif()
endfunction()

add_library(VEngine STATIC)

target_sources(VEngine
    PRIVATE
        Engine/Runtime/Core/Application.cpp
        Engine/Runtime/Core/Version.cpp
        Engine/RHI/Common/RhiTypes.cpp
        Engine/RHI/Common/RhiUtils.cpp
    PUBLIC
        Engine/Runtime/Core/Application.h
        Engine/Runtime/Core/Version.h
        Engine/RHI/Common/RhiDevice.h
        Engine/RHI/Common/RhiTypes.h
        Engine/RHI/Common/RhiUtils.h
)

target_include_directories(VEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(VEngine
    PUBLIC
        cxx_std_20
)

target_compile_definitions(VEngine
    PUBLIC
        VE_VERSION_STRING="${PROJECT_VERSION}"
        VE_ENABLE_D3D11=$<BOOL:${VE_ENABLE_D3D11}>
        VE_ENABLE_D3D12=$<BOOL:${VE_ENABLE_D3D12}>
        VE_ENABLE_METAL=$<BOOL:${VE_ENABLE_METAL}>
        VE_ENABLE_SCRIPTING=$<BOOL:${VE_ENABLE_SCRIPTING}>
)

if(WIN32)
    target_compile_definitions(VEngine
        PUBLIC
            VE_PLATFORM_WINDOWS=1
            VE_PLATFORM_IOS=0
            VE_PLATFORM_APPLE=0
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(VEngine
        PUBLIC
            VE_PLATFORM_WINDOWS=0
            VE_PLATFORM_IOS=1
            VE_PLATFORM_APPLE=1
    )
elseif(APPLE)
    target_compile_definitions(VEngine
        PUBLIC
            VE_PLATFORM_WINDOWS=0
            VE_PLATFORM_IOS=0
            VE_PLATFORM_APPLE=1
    )
else()
    target_compile_definitions(VEngine
        PUBLIC
            VE_PLATFORM_WINDOWS=0
            VE_PLATFORM_IOS=0
            VE_PLATFORM_APPLE=0
    )
endif()

ve_setup_boost_library(VEngine)

if(WIN32 AND VE_ENABLE_D3D11)
    target_sources(VEngine
        PRIVATE
            Engine/RHI/D3D11/D3D11Rhi.cpp
        PUBLIC
            Engine/RHI/D3D11/D3D11Rhi.h
    )

    target_link_libraries(VEngine
        PUBLIC
            d3d11
            dxgi
            d3dcompiler
    )
endif()

if(WIN32 AND VE_ENABLE_D3D12)
    target_sources(VEngine
        PRIVATE
            Engine/RHI/D3D12/D3D12Rhi.cpp
        PUBLIC
            Engine/RHI/D3D12/D3D12Rhi.h
    )

    target_link_libraries(VEngine
        PUBLIC
            d3d12
            dxgi
            d3dcompiler
    )
endif()

if(APPLE AND VE_ENABLE_METAL)
    target_sources(VEngine
        PRIVATE
            Engine/RHI/Metal/MetalRhi.mm
        PUBLIC
            Engine/RHI/Metal/MetalRhi.h
    )

    target_link_libraries(VEngine
        PUBLIC
            "-framework Foundation"
            "-framework Metal"
            "-framework QuartzCore"
    )
endif()

ve_configure_target(VEngine)

if(VE_BUILD_PLAYER)
    if(WIN32)
        add_executable(VEnginePlayer
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
endif()

if(VE_BUILD_EDITOR)
    if(WIN32)
        add_executable(VEngineEditor
            Editor/Windows/WindowsEditor.cpp
        )

        target_link_libraries(VEngineEditor
            PRIVATE
                VEngine
        )

        ve_configure_target(VEngineEditor)
    else()
        message(STATUS "VEngineEditor is only built on Windows.")
    endif()
endif()

if(VE_BUILD_TESTS)
    if(WIN32)
        enable_testing()

        add_executable(VEngineTests
            Tests/Unit/CoreTests.cpp
        )

        target_link_libraries(VEngineTests
            PRIVATE
                VEngine
        )

        ve_configure_target(VEngineTests)

        add_test(
            NAME VEngineTests
            COMMAND VEngineTests
        )
    else()
        message(STATUS "VEngineTests is only built on Windows in the first stage.")
    endif()
endif()

if(VE_BUILD_RHI_DEMOS)
    if(WIN32)
        if(VE_ENABLE_D3D11)
            add_executable(VEngineRhiD3D11TriangleDemo
                Tests/Demos/RHI/Windows/D3D11TriangleDemo.cpp
                Tests/Demos/RHI/Windows/WindowsTriangleDemo.cpp
                Tests/Demos/RHI/Windows/WindowsTriangleDemo.h
            )

            target_link_libraries(VEngineRhiD3D11TriangleDemo
                PRIVATE
                    VEngine
                    user32
            )

            ve_configure_target(VEngineRhiD3D11TriangleDemo)
        endif()

        if(VE_ENABLE_D3D12)
            add_executable(VEngineRhiD3D12TriangleDemo
                Tests/Demos/RHI/Windows/D3D12TriangleDemo.cpp
                Tests/Demos/RHI/Windows/WindowsTriangleDemo.cpp
                Tests/Demos/RHI/Windows/WindowsTriangleDemo.h
            )

            target_link_libraries(VEngineRhiD3D12TriangleDemo
                PRIVATE
                    VEngine
                    user32
            )

            ve_configure_target(VEngineRhiD3D12TriangleDemo)
        endif()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND VE_ENABLE_METAL)
        enable_language(OBJCXX)

        find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
        find_library(UIKIT_FRAMEWORK UIKit REQUIRED)

        add_executable(VEngineRhiMetalTriangleDemo MACOSX_BUNDLE
            Tests/Demos/RHI/iOS/MetalTriangleDemo.mm
        )

        target_link_libraries(VEngineRhiMetalTriangleDemo
            PRIVATE
                VEngine
                ${FOUNDATION_FRAMEWORK}
                ${UIKIT_FRAMEWORK}
                "-framework QuartzCore"
                "-framework Metal"
        )

        set_target_properties(VEngineRhiMetalTriangleDemo
            PROPERTIES
                MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Tests/Demos/RHI/iOS/Info.plist.in
                XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.vengine.tests.rhi.metaltriangle"
                XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
                XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphonesimulator iphoneos"
        )

        ve_configure_target(VEngineRhiMetalTriangleDemo)
    endif()
endif()

if(VE_BUILD_TOOLS)
    add_executable(VEngineAssetTool
        Tools/AssetTool/AssetTool.cpp
    )

    target_link_libraries(VEngineAssetTool
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineAssetTool)

    add_executable(VEngineShaderTool
        Tools/ShaderTool/ShaderTool.cpp
    )

    target_link_libraries(VEngineShaderTool
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineShaderTool)
endif()

if(VE_BUILD_IOS_PLAYER)
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
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Player/iOS/Info.plist.in
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${VE_IOS_BUNDLE_IDENTIFIER}
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphonesimulator iphoneos"
    )

    ve_configure_target(VEngineIOSPlayer)
endif()
