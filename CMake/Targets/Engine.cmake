include_guard(GLOBAL)

function(ve_add_engine)
    if(TARGET VEngine)
        return()
    endif()

    add_library(VEngine STATIC)

    target_sources(VEngine
        PRIVATE
            Engine/Runtime/Application/Application.cpp
            Engine/Runtime/Application/EngineRuntime.cpp
            Engine/Runtime/Asset/AssetDatabase.cpp
            Engine/Runtime/Asset/AssetGuid.cpp
            Engine/Runtime/Asset/NativeAssetIO.cpp
            Engine/Runtime/Asset/SceneAssetLoader.cpp
            Engine/Runtime/Core/Assert.cpp
            Engine/Runtime/Core/Error.cpp
            Engine/Runtime/Core/Version.cpp
            Engine/Runtime/FileSystem/FileSystem.cpp
            Engine/Runtime/FileSystem/Path.cpp
            Engine/Runtime/GameThread/GameThreadSystem.cpp
            Engine/Runtime/IO/IOSystem.cpp
            Engine/Runtime/Jobs/JobSystem.cpp
            Engine/Runtime/Logging/Log.cpp
            Engine/Runtime/Memory/PoolAllocator.cpp
            Engine/Runtime/Platform/Window.cpp
            Engine/Runtime/Physics/ColliderComponent.cpp
            Engine/Runtime/Physics/PhysicsGeometry.cpp
            Engine/Runtime/Physics/PhysicsSystem.cpp
            Engine/Runtime/Physics/PhysicsWorld.cpp
            Engine/Runtime/Physics/RigidBodyComponent.cpp
            Engine/Runtime/Reflection/ReflectionRegistry.cpp
            Engine/Runtime/Render/RenderCommandQueue.cpp
            Engine/Runtime/Render/RenderSystem.cpp
            Engine/Runtime/Resource/BuiltInResources.cpp
            Engine/Runtime/Resource/ResourceManager.cpp
            Engine/Runtime/Scene/Component.cpp
            Engine/Runtime/Scene/GameObject.cpp
            Engine/Runtime/Scene/RenderComponents.cpp
            Engine/Runtime/Scene/Scene.cpp
            Engine/Runtime/Scene/SceneRenderExtractor.cpp
            Engine/Runtime/Scene/Serialization/SceneSerialization.cpp
            Engine/Runtime/Scene/TransformComponent.cpp
            Engine/Runtime/Scripting/ScriptBridge.cpp
            Engine/Runtime/Scripting/ScriptComponent.cpp
            Engine/Runtime/Scripting/ScriptContext.cpp
            Engine/Runtime/Scripting/ScriptHost.cpp
            Engine/Runtime/Scripting/ScriptProject.cpp
            Engine/Runtime/Threading/Synchronization.cpp
            Engine/Runtime/Threading/Thread.cpp
            Engine/Runtime/Time/Time.cpp
            Engine/RHI/Common/RhiTypes.cpp
            Engine/RHI/Common/RhiUtils.cpp
        PUBLIC
            Engine/Runtime/Application/Application.h
            Engine/Runtime/Application/EngineRuntime.h
            Engine/Runtime/Asset/AssetDatabase.h
            Engine/Runtime/Asset/AssetGuid.h
            Engine/Runtime/Asset/AssetReference.h
            Engine/Runtime/Asset/NativeAssetIO.h
            Engine/Runtime/Asset/SceneAssetLoader.h
            Engine/Runtime/Core/Assert.h
            Engine/Runtime/Core/BuildConfig.h
            Engine/Runtime/Core/Compiler.h
            Engine/Runtime/Core/EnumFlags.h
            Engine/Runtime/Core/Error.h
            Engine/Runtime/Core/NonCopyable.h
            Engine/Runtime/Core/Platform.h
            Engine/Runtime/Core/Result.h
            Engine/Runtime/Core/ScopeExit.h
            Engine/Runtime/Core/SourceLocation.h
            Engine/Runtime/Core/Types.h
            Engine/Runtime/Core/Version.h
            Engine/Runtime/FileSystem/FileSystem.h
            Engine/Runtime/FileSystem/Path.h
            Engine/Runtime/GameThread/GameThreadSystem.h
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
            Engine/Runtime/Physics/ColliderComponent.h
            Engine/Runtime/Physics/PhysicsGeometry.h
            Engine/Runtime/Physics/PhysicsSystem.h
            Engine/Runtime/Physics/PhysicsWorld.h
            Engine/Runtime/Physics/RigidBodyComponent.h
            Engine/Runtime/Reflection/ReflectionRegistry.h
            Engine/Runtime/Render/RenderCommandQueue.h
            Engine/Runtime/Render/EditorUiFrame.h
            Engine/Runtime/Render/RenderSystem.h
            Engine/Runtime/Resource/BuiltInResources.h
            Engine/Runtime/Resource/ResourceHandle.h
            Engine/Runtime/Resource/ResourceManager.h
            Engine/Runtime/Scene/Component.h
            Engine/Runtime/Scene/GameObject.h
            Engine/Runtime/Scene/RenderComponents.h
            Engine/Runtime/Scene/Scene.h
            Engine/Runtime/Scene/SceneRenderExtractor.h
            Engine/Runtime/Scene/SceneRenderSnapshot.h
            Engine/Runtime/Scene/SceneTypes.h
            Engine/Runtime/Scene/Serialization/SceneSerialization.h
            Engine/Runtime/Scene/TransformComponent.h
            Engine/Runtime/Scripting/ScriptBridge.h
            Engine/Runtime/Scripting/ScriptComponent.h
            Engine/Runtime/Scripting/ScriptContext.h
            Engine/Runtime/Scripting/ScriptHost.h
            Engine/Runtime/Scripting/ScriptProject.h
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
            VE_BUILD_CONFIGURATION="$<CONFIG>"
            VE_DEFAULT_PROJECT_DIR="${PROJECT_SOURCE_DIR}/Examples/AssetPipelineSample"
            VE_ENGINE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
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
        ve_setup_dotnet_hosting(VEngine)
        ve_add_managed_scripting_targets()
        add_dependencies(VEngine VEngineScriptAPI)

        target_sources(VEngine
            PRIVATE
                Engine/Runtime/Platform/Windows/Win32DebugConsole.cpp
                Engine/Runtime/Platform/Windows/Win32MessageLoop.cpp
                Engine/Runtime/Platform/Windows/Win32Window.cpp
                Engine/Runtime/Scripting/DotNetHostWindows.cpp
            PUBLIC
                Engine/Runtime/Platform/Windows/Win32DebugConsole.h
                Engine/Runtime/Platform/Windows/Win32MessageLoop.h
                Engine/Runtime/Platform/Windows/Win32Window.h
                Engine/Runtime/Scripting/DotNetHost.h
        )

        target_link_libraries(VEngine
            PUBLIC
                user32
        )
    else()
        target_sources(VEngine
            PRIVATE
                Engine/Runtime/Scripting/DotNetHostStub.cpp
            PUBLIC
                Engine/Runtime/Scripting/DotNetHost.h
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
