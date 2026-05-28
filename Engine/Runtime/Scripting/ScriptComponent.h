#pragma once

#include "Engine/Runtime/Scene/Component.h"
#include "Engine/Runtime/Scripting/ScriptBridge.h"
#include "Engine/Runtime/Scripting/ScriptHost.h"

#include <string>

namespace ve
{
    class ReflectionRegistry;
    class ScriptContext;

    class ScriptComponent final : public Component
    {
    public:
        ScriptComponent() = default;
        ~ScriptComponent() override;

        [[nodiscard]] const std::string& GetScriptTypeName() const noexcept;
        void SetScriptTypeName(std::string scriptTypeName);

        [[nodiscard]] const std::string& GetAssemblyName() const noexcept;
        void SetAssemblyName(std::string assemblyName);

        [[nodiscard]] bool IsScriptValid() const noexcept;
        [[nodiscard]] const std::string& GetLastError() const noexcept;

    protected:
        void OnCreate() override;
        void OnDestroy() override;
        void OnEnable() override;
        void OnDisable() override;
        void OnUpdate() override;

    private:
        [[nodiscard]] ScriptContext* GetActiveScriptContext() noexcept;
        [[nodiscard]] bool EnsureManagedInstance(bool reportMissingContext);
        [[nodiscard]] bool InvokeManagedLifecycle(ScriptLifecycleMethod lifecycle, float deltaSeconds = 0.0f);
        void DestroyManagedInstance() noexcept;
        void ReportFailure(std::string message);
        void ResetRuntimeState() noexcept;

        std::string scriptTypeName_;
        std::string assemblyName_;
        std::string lastError_;
        ScriptObjectHandle nativeHandle_ = InvalidScriptObjectHandle;
        ScriptInstanceId instanceId_ = InvalidScriptInstanceId;
        bool nativeLifecycleCreated_ = false;
        bool scriptValid_ = false;
        bool failed_ = false;
    };

    void RegisterScriptReflectionTypes(ReflectionRegistry& registry);
} // namespace ve
