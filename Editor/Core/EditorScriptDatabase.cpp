#include "Editor/Core/EditorScriptDatabase.h"

#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Scripting/ScriptingSystem.h"

#include <boost/json.hpp>

namespace ve::editor
{
    void EditorScriptDatabase::Clear() noexcept
    {
        scriptTypes_.clear();
    }

    ErrorCode EditorScriptDatabase::RefreshFromScriptingSystem(ScriptingSystem& scriptingSystem)
    {
        scriptTypes_ = scriptingSystem.GetAvailableScriptTypes();
        return ErrorCode::None;
    }

    ErrorCode EditorScriptDatabase::SaveManifest(const Path& manifestPath, const Path& assemblyPath) const
    {
        boost::json::object root;
        root["schemaVersion"] = 1;
        root["assemblyPath"] = assemblyPath.GetString();

        boost::json::array types;
        for (const ScriptTypeInfo& typeInfo : scriptTypes_)
        {
            boost::json::object object;
            object["typeName"] = typeInfo.typeName;
            object["displayName"] = typeInfo.displayName;
            types.push_back(std::move(object));
        }
        root["scriptTypes"] = std::move(types);

        return FileSystem::WriteTextFile(manifestPath, JsonUtils::SerializePretty(root));
    }

    const std::vector<ScriptTypeInfo>& EditorScriptDatabase::GetScriptTypes() const noexcept
    {
        return scriptTypes_;
    }

    const ScriptTypeInfo* EditorScriptDatabase::FindScriptType(std::string_view typeName) const noexcept
    {
        for (const ScriptTypeInfo& typeInfo : scriptTypes_)
        {
            if (typeInfo.typeName == typeName)
            {
                return &typeInfo;
            }
        }

        return nullptr;
    }
} // namespace ve::editor
