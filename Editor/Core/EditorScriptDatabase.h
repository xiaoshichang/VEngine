#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Scripting/ScriptingTypes.h"

#include <string_view>
#include <vector>

namespace ve
{
    class ScriptingSystem;
}

namespace ve::editor
{
    class EditorScriptDatabase
    {
    public:
        void Clear() noexcept;
        [[nodiscard]] ErrorCode RefreshFromScriptingSystem(ScriptingSystem& scriptingSystem);
        [[nodiscard]] ErrorCode SaveManifest(const Path& manifestPath, const Path& assemblyPath) const;

        [[nodiscard]] const std::vector<ScriptTypeInfo>& GetScriptTypes() const noexcept;
        [[nodiscard]] const ScriptTypeInfo* FindScriptType(std::string_view typeName) const noexcept;

    private:
        std::vector<ScriptTypeInfo> scriptTypes_;
    };
} // namespace ve::editor
