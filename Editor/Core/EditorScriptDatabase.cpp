#include "Editor/Core/EditorScriptDatabase.h"

#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Scripting/ScriptingSystem.h"

#include <boost/json.hpp>

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] const char* ToString(ScriptFieldKind kind) noexcept
        {
            switch (kind)
            {
            case ScriptFieldKind::Bool:
                return "Bool";
            case ScriptFieldKind::Int:
                return "Int";
            case ScriptFieldKind::Float:
                return "Float";
            case ScriptFieldKind::String:
                return "String";
            case ScriptFieldKind::Vector3:
                return "Vector3";
            case ScriptFieldKind::Color:
                return "Color";
            case ScriptFieldKind::Enum:
                return "Enum";
            case ScriptFieldKind::Unsupported:
                return "Unsupported";
            }

            return "Unsupported";
        }
    } // namespace

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
            boost::json::array fields;
            for (const ScriptFieldInfo& fieldInfo : typeInfo.fields)
            {
                boost::json::object field;
                field["name"] = fieldInfo.name;
                field["displayName"] = fieldInfo.displayName;
                field["kind"] = ToString(fieldInfo.kind);
                field["managedTypeName"] = fieldInfo.managedTypeName;

                boost::json::array enumNames;
                for (const std::string& enumName : fieldInfo.enumNames)
                {
                    enumNames.push_back(boost::json::string(enumName));
                }
                field["enumNames"] = std::move(enumNames);
                if (!fieldInfo.defaultValueJson.empty())
                {
                    Result<boost::json::value> defaultValue = JsonUtils::Parse(fieldInfo.defaultValueJson);
                    if (defaultValue)
                    {
                        field["defaultValue"] = defaultValue.MoveValue();
                    }
                }
                fields.push_back(std::move(field));
            }
            object["fields"] = std::move(fields);
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
