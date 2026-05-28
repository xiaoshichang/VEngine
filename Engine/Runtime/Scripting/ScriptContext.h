#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Scripting/ScriptBridge.h"
#include "Engine/Runtime/Scripting/ScriptHost.h"

#include <string_view>
#include <unordered_set>

namespace ve
{
    class ScriptComponent;

    class ScriptContext : public NonMovable
    {
    public:
        explicit ScriptContext(ScriptHost& host);
        ~ScriptContext();

        [[nodiscard]] Result<ScriptOperationResult> LoadProjectAssembly(const Path& assemblyPath);
        [[nodiscard]] bool IsProjectAssemblyLoaded() const noexcept;
        [[nodiscard]] const Path& GetProjectAssemblyPath() const noexcept;

        [[nodiscard]] ScriptObjectHandle RegisterScriptComponent(ScriptComponent& component);
        void UnregisterScriptComponent(ScriptObjectHandle handle, const ScriptComponent& component) noexcept;

        [[nodiscard]] Result<ScriptInstanceId> CreateScriptInstance(std::string_view scriptTypeName,
                                                                    ScriptObjectHandle nativeHandle);
        [[nodiscard]] Result<ScriptOperationResult> InvokeLifecycle(ScriptInstanceId instanceId,
                                                                    ScriptLifecycleMethod lifecycle,
                                                                    float deltaSeconds);
        [[nodiscard]] Result<ScriptOperationResult> DestroyScriptInstance(ScriptInstanceId instanceId);

    private:
        ScriptHost* host_ = nullptr;
        Path projectAssemblyPath_;
        bool projectAssemblyLoaded_ = false;
        std::unordered_set<ScriptInstanceId> activeInstances_;
    };
} // namespace ve
