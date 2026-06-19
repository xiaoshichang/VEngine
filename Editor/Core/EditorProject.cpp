#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/Core/Version.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <boost/json.hpp>

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] std::string ReadString(const boost::json::object& object, boost::json::string_view key, std::string fallback = {})
        {
            if (const boost::json::value* value = object.if_contains(key); value != nullptr && value->is_string())
            {
                return std::string(value->as_string());
            }

            return fallback;
        }

        [[nodiscard]] const char* GetCurrentEngineVersion() noexcept
        {
            return GetBuildInfo().version;
        }

        [[nodiscard]] boost::json::object ToJson(const EditorProjectDescriptor& descriptor)
        {
            boost::json::object object;
            object["schemaVersion"] = 1;
            object["name"] = descriptor.name;
            object["engineVersion"] = descriptor.engineVersion.empty() ? GetCurrentEngineVersion() : descriptor.engineVersion;
            object["startScene"] = descriptor.startScene;
            return object;
        }
    } // namespace

    Path EditorProject::GetDescriptorPath(const Path& projectRoot)
    {
        return projectRoot / DescriptorFilename;
    }

    Path EditorProject::GetAssetsPath(const Path& projectRoot)
    {
        return projectRoot / AssetsDirectoryName;
    }

    Path EditorProject::GetLibraryPath(const Path& projectRoot)
    {
        return projectRoot / LibraryDirectoryName;
    }

    bool EditorProject::IsProjectRoot(const Path& projectRoot)
    {
        return FileSystem::IsDirectory(projectRoot) && FileSystem::IsFile(GetDescriptorPath(projectRoot));
    }

    ErrorCode EditorProject::EnsureLayout(const Path& projectRoot)
    {
        if (projectRoot.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        ErrorCode result = FileSystem::CreateDirectories(projectRoot);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = FileSystem::CreateDirectories(GetAssetsPath(projectRoot));
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = FileSystem::CreateDirectories(GetLibraryPath(projectRoot));
        if (result != ErrorCode::None)
        {
            return result;
        }

        if (!FileSystem::Exists(GetDescriptorPath(projectRoot)))
        {
            EditorProjectDescriptor descriptor;
            descriptor.name = projectRoot.GetFilename();
            descriptor.engineVersion = GetCurrentEngineVersion();
            return SaveDescriptor(projectRoot, descriptor);
        }

        return ErrorCode::None;
    }

    ErrorCode EditorProject::SaveDescriptor(const Path& projectRoot, const EditorProjectDescriptor& descriptor)
    {
        if (projectRoot.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        const boost::json::value value(ToJson(descriptor));
        return FileSystem::WriteTextFile(GetDescriptorPath(projectRoot), JsonUtils::SerializePretty(value));
    }

    Result<EditorProjectDescriptor> EditorProject::LoadDescriptor(const Path& projectRoot)
    {
        Result<std::string> textResult = FileSystem::ReadTextFile(GetDescriptorPath(projectRoot));
        if (!textResult)
        {
            return Result<EditorProjectDescriptor>::Failure(textResult.GetError());
        }

        Result<boost::json::value> jsonResult = JsonUtils::Parse(textResult.GetValue());
        if (!jsonResult)
        {
            return Result<EditorProjectDescriptor>::Failure(jsonResult.GetError());
        }

        const boost::json::value& value = jsonResult.GetValue();
        if (!value.is_object())
        {
            return Result<EditorProjectDescriptor>::Failure(Error(ErrorCode::InvalidArgument, "Project descriptor root must be a JSON object."));
        }

        const boost::json::object& object = value.as_object();
        EditorProjectDescriptor descriptor;
        descriptor.name = ReadString(object, "name", projectRoot.GetFilename());
        descriptor.engineVersion = ReadString(object, "engineVersion");
        descriptor.startScene = ReadString(object, "startScene", descriptor.startScene);
        return Result<EditorProjectDescriptor>::Success(std::move(descriptor));
    }
} // namespace ve::editor
