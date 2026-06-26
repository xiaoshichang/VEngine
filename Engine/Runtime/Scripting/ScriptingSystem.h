#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Scripting/ScriptingSystemBackend.h"
#include "Engine/Runtime/Scripting/ScriptingTypes.h"

#include <memory>
#include <string>
#include <string_view>

namespace ve
{
    class ScriptingSystem final : public NonMovable
    {
    public:
        ScriptingSystem();
        ~ScriptingSystem();

        [[nodiscard]] ErrorCode Initialize(const ScriptingSystemInitParam& initParam);
        void Shutdown() noexcept;
        [[nodiscard]] ScriptingBackendType GetBackendType() const noexcept;

        [[nodiscard]] ErrorCode LoadAssembly(const ScriptingAssemblyLoadDesc& desc);
        [[nodiscard]] ErrorCode LoadProjectAssembly(const ScriptingProjectAssemblyLoadDesc& desc);
        void UnloadProjectAssembly() noexcept;
        [[nodiscard]] std::vector<ScriptTypeInfo> GetAvailableScriptTypes();
        [[nodiscard]] Result<ScriptInstanceHandle> CreateScriptInstance(const ScriptInstanceDesc& desc);
        void DestroyScriptInstance(ScriptInstanceHandle script) noexcept;
        void InvokeScriptEvent(ScriptInstanceHandle script, ScriptLifecycleEvent event, Float32 deltaSeconds = 0.0f) noexcept;
        [[nodiscard]] Result<std::string> GetScriptFieldsJson(ScriptInstanceHandle script);
        [[nodiscard]] ErrorCode SetScriptFieldsJson(ScriptInstanceHandle script, std::string_view fieldsJson);
        [[nodiscard]] ErrorCode SetScriptFieldJson(ScriptInstanceHandle script, std::string_view fieldName, std::string_view valueJson);

    private:
        [[nodiscard]] static std::unique_ptr<ScriptingSystemBackend> CreateBackend(ScriptingBackendType backendType);

        std::unique_ptr<ScriptingSystemBackend> backend_;
        bool initialized_ = false;
    };
} // namespace ve
