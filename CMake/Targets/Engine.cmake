include_guard(GLOBAL)

function(ve_add_engine)
    if(TARGET VEngine)
        return()
    endif()

    add_library(VEngine STATIC)

    target_sources(VEngine
        PRIVATE
            Engine/Runtime/Application/Application.cpp
            Engine/Runtime/Application/ApplicationCommandQueue.cpp
            Engine/Runtime/Application/EngineRuntime.cpp
            Engine/Runtime/Core/Assert.cpp
            Engine/Runtime/Core/Error.cpp
            Engine/Runtime/Core/Guid.cpp
            Engine/Runtime/Core/JsonUtils.cpp
            Engine/Runtime/Core/Version.cpp
            Engine/Runtime/FileSystem/FileSystem.cpp
            Engine/Runtime/FileSystem/Path.cpp
            Engine/Runtime/Input/InputSystem.cpp
            Engine/Runtime/IO/IOSystem.cpp
            Engine/Runtime/Jobs/JobSystem.cpp
            Engine/Runtime/Logging/Log.cpp
            Engine/Runtime/Memory/PoolAllocator.cpp
            Engine/Runtime/Platform/Window.cpp
            Engine/Runtime/Render/BaseRenderer.cpp
            Engine/Runtime/Render/RenderTarget.cpp
            Engine/Runtime/Render/RenderTexture.cpp
            Engine/Runtime/Render/RenderCommandQueue.cpp
            Engine/Runtime/Render/RenderFramePipeline.cpp
            Engine/Runtime/Render/RenderPass.cpp
            Engine/Runtime/Render/RenderResource.cpp
            Engine/Runtime/Render/RenderScene.cpp
            Engine/Runtime/Render/RenderSystem.cpp
            Engine/Runtime/Render/ViewportClient.cpp
            Engine/Runtime/Resource/AssetID.cpp
            Engine/Runtime/Resource/AssetRef.cpp
            Engine/Runtime/Resource/AssetManifest.cpp
            Engine/Runtime/Resource/ResourceObject.cpp
            Engine/Runtime/Resource/ResourceSystem.cpp
            Engine/Runtime/Resource/RuntimeAssetLoader.cpp
            Engine/Runtime/Scene/CameraComponent.cpp
            Engine/Runtime/Scene/Component.cpp
            Engine/Runtime/Scene/GameObject.cpp
            Engine/Runtime/Scene/LightComponent.cpp
            Engine/Runtime/Scene/MeshRenderComponent.cpp
            Engine/Runtime/Scene/Scene.cpp
            Engine/Runtime/Scene/SceneSerialization.cpp
            Engine/Runtime/Scene/SceneSystem.cpp
            Engine/Runtime/Scene/TransformComponent.cpp
            Engine/Runtime/Threading/Synchronization.cpp
            Engine/Runtime/Threading/Thread.cpp
            Engine/Runtime/Time/Time.cpp
            Engine/RHI/Common/RhiTypes.cpp
            Engine/RHI/Common/RhiUtils.cpp
        PUBLIC
            Engine/Runtime/Application/Application.h
            Engine/Runtime/Application/ApplicationCommandQueue.h
            Engine/Runtime/Application/EngineRuntime.h
            Engine/Runtime/Core/Assert.h
            Engine/Runtime/Core/BuildConfig.h
            Engine/Runtime/Core/Compiler.h
            Engine/Runtime/Core/EnumFlags.h
            Engine/Runtime/Core/Error.h
            Engine/Runtime/Core/Guid.h
            Engine/Runtime/Core/JsonUtils.h
            Engine/Runtime/Core/NonCopyable.h
            Engine/Runtime/Core/Platform.h
            Engine/Runtime/Core/Result.h
            Engine/Runtime/Core/ScopeExit.h
            Engine/Runtime/Core/SourceLocation.h
            Engine/Runtime/Core/Types.h
            Engine/Runtime/Core/Version.h
            Engine/Runtime/FileSystem/FileSystem.h
            Engine/Runtime/FileSystem/Path.h
            Engine/Runtime/Input/InputSystem.h
            Engine/Runtime/Input/OSEvent.h
            Engine/Runtime/IO/IOSystem.h
            Engine/Runtime/Jobs/JobSystem.h
            Engine/Runtime/Logging/Log.h
            Engine/Runtime/Math/Math.h
            Engine/Runtime/Math/Matrix44.h
            Engine/Runtime/Math/Quaternion.h
            Engine/Runtime/Math/Vector2.h
            Engine/Runtime/Math/Vector3.h
            Engine/Runtime/Math/Vector4.h
            Engine/Runtime/Memory/PoolAllocator.h
            Engine/Runtime/Platform/Window.h
            Engine/Runtime/Render/BaseRenderer.h
            Engine/Runtime/Render/RenderTarget.h
            Engine/Runtime/Render/RenderTexture.h
            Engine/Runtime/Render/RenderCommandQueue.h
            Engine/Runtime/Render/RenderFramePipeline.h
            Engine/Runtime/Render/RenderPass.h
            Engine/Runtime/Render/RenderResource.h
            Engine/Runtime/Render/RenderScene.h
            Engine/Runtime/Render/RenderSystem.h
            Engine/Runtime/Render/ViewportClient.h
            Engine/Runtime/Resource/AssetID.h
            Engine/Runtime/Resource/AssetRecord.h
            Engine/Runtime/Resource/AssetRef.h
            Engine/Runtime/Resource/AssetManifest.h
            Engine/Runtime/Resource/ResourceObject.h
            Engine/Runtime/Resource/ResourceSystem.h
            Engine/Runtime/Resource/RuntimeAssetLoader.h
            Engine/Runtime/Scene/CameraComponent.h
            Engine/Runtime/Scene/Component.h
            Engine/Runtime/Scene/GameObject.h
            Engine/Runtime/Scene/LightComponent.h
            Engine/Runtime/Scene/MeshRenderComponent.h
            Engine/Runtime/Scene/Scene.h
            Engine/Runtime/Scene/SceneSerialization.h
            Engine/Runtime/Scene/SceneSystem.h
            Engine/Runtime/Scene/TransformComponent.h
            Engine/Runtime/Threading/Atomic.h
            Engine/Runtime/Threading/LockFreeMpscQueue.h
            Engine/Runtime/Threading/LockFreeSpscQueue.h
            Engine/Runtime/Threading/Synchronization.h
            Engine/Runtime/Threading/Thread.h
            Engine/Runtime/Time/Time.h
            Engine/RHI/Common/RhiDevice.h
            Engine/RHI/Common/RhiTypes.h
            Engine/RHI/Common/RhiUtils.h
    )

    target_include_directories(VEngine
        PUBLIC
            ${PROJECT_SOURCE_DIR}
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

    if(WIN32)
        target_sources(VEngine
            PRIVATE
                Engine/Runtime/Platform/Windows/Win32DebugConsole.cpp
                Engine/Runtime/Platform/Windows/Win32MessageLoop.cpp
                Engine/Runtime/Platform/Windows/Win32Window.cpp
            PUBLIC
                Engine/Runtime/Platform/Windows/Win32DebugConsole.h
                Engine/Runtime/Platform/Windows/Win32MessageLoop.h
                Engine/Runtime/Platform/Windows/Win32Window.h
        )

        target_link_libraries(VEngine
            PUBLIC
                user32
        )
    endif()

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
        enable_language(OBJCXX)

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
endfunction()
