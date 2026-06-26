#include "Engine/Runtime/Scripting/ScriptingSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/Scripting/IOSAOTScriptingBackend.h"
#include "Engine/Runtime/Scripting/WindowsJITScriptingBackend.h"

#include <utility>

namespace ve
{
    ScriptingSystem::ScriptingSystem() = default;

    ScriptingSystem::~ScriptingSystem()
    {
        Shutdown();
    }

    ErrorCode ScriptingSystem::Initialize(const ScriptingSystemInitParam& initParam)
    {
        if (initialized_)
        {
            return ErrorCode::InvalidState;
        }

        backend_ = CreateBackend(initParam.backendType);
        if (backend_ == nullptr)
        {
            return ErrorCode::Unsupported;
        }

        const ErrorCode result = backend_->Initialize(initParam);
        if (result != ErrorCode::None)
        {
            backend_.reset();
            return result;
        }

        initialized_ = true;
        return ErrorCode::None;
    }

    void ScriptingSystem::Shutdown() noexcept
    {
        if (backend_ != nullptr)
        {
            backend_->Shutdown();
            backend_.reset();
        }

        initialized_ = false;
    }

    ScriptingBackendType ScriptingSystem::GetBackendType() const noexcept
    {
        VE_ASSERT_MESSAGE(initialized_ && backend_ != nullptr, "ScriptingSystem::GetBackendType requires an initialized scripting system.");
        return backend_->GetBackendType();
    }

    ErrorCode ScriptingSystem::LoadAssembly(const ScriptingAssemblyLoadDesc& desc)
    {
        VE_ASSERT_MESSAGE(initialized_ && backend_ != nullptr, "ScriptingSystem::LoadAssembly requires an initialized scripting system.");
        return backend_->LoadAssembly(desc);
    }

    ErrorCode ScriptingSystem::LoadProjectAssembly(const ScriptingProjectAssemblyLoadDesc& desc)
    {
        VE_ASSERT_MESSAGE(initialized_ && backend_ != nullptr, "ScriptingSystem::LoadProjectAssembly requires an initialized scripting system.");
        return backend_->LoadProjectAssembly(desc);
    }

    void ScriptingSystem::UnloadProjectAssembly() noexcept
    {
        VE_ASSERT_MESSAGE(initialized_ && backend_ != nullptr, "ScriptingSystem::UnloadProjectAssembly requires an initialized scripting system.");
        backend_->UnloadProjectAssembly();
    }

    std::vector<ScriptTypeInfo> ScriptingSystem::GetAvailableScriptTypes()
    {
        VE_ASSERT_MESSAGE(initialized_ && backend_ != nullptr, "ScriptingSystem::GetAvailableScriptTypes requires an initialized scripting system.");
        return backend_->GetAvailableScriptTypes();
    }

    Result<ScriptInstanceHandle> ScriptingSystem::CreateScriptInstance(const ScriptInstanceDesc& desc)
    {
        VE_ASSERT_MESSAGE(initialized_ && backend_ != nullptr, "ScriptingSystem::CreateScriptInstance requires an initialized scripting system.");
        return backend_->CreateScriptInstance(desc);
    }

    void ScriptingSystem::DestroyScriptInstance(ScriptInstanceHandle script) noexcept
    {
        VE_ASSERT_MESSAGE(initialized_ && backend_ != nullptr, "ScriptingSystem::DestroyScriptInstance requires an initialized scripting system.");
        backend_->DestroyScriptInstance(script);
    }

    void ScriptingSystem::InvokeScriptEvent(ScriptInstanceHandle script, ScriptLifecycleEvent event, Float32 deltaSeconds) noexcept
    {
        VE_ASSERT_MESSAGE(initialized_ && backend_ != nullptr, "ScriptingSystem::InvokeScriptEvent requires an initialized scripting system.");
        backend_->InvokeScriptEvent(script, event, deltaSeconds);
    }

    std::unique_ptr<ScriptingSystemBackend> ScriptingSystem::CreateBackend(ScriptingBackendType backendType)
    {
        ScriptingBackendType resolvedBackend = backendType;
        if (resolvedBackend == ScriptingBackendType::Auto)
        {
#if VE_PLATFORM_WINDOWS
            resolvedBackend = ScriptingBackendType::WindowsJIT;
#elif VE_PLATFORM_IOS
            resolvedBackend = ScriptingBackendType::IOSAOT;
#else
            return nullptr;
#endif
        }

        switch (resolvedBackend)
        {
        case ScriptingBackendType::WindowsJIT:
#if VE_PLATFORM_WINDOWS
            return std::make_unique<WindowsJITScriptingBackend>();
#else
            return nullptr;
#endif
        case ScriptingBackendType::IOSAOT:
            return std::make_unique<IOSAOTScriptingBackend>();
        case ScriptingBackendType::Auto:
            return nullptr;
        }

        return nullptr;
    }
} // namespace ve
