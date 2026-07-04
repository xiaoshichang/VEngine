#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/Core/Version.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <boost/json.hpp>

#include <cctype>
#include <cstdlib>
#include <utility>

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

        [[nodiscard]] const boost::json::object* ReadObject(const boost::json::object& object, boost::json::string_view key)
        {
            const boost::json::value* value = object.if_contains(key);
            return value != nullptr && value->is_object() ? &value->as_object() : nullptr;
        }

        [[nodiscard]] std::string SanitizeBundleIdentifierSegment(std::string text)
        {
            std::string result;
            result.reserve(text.size());

            bool previousDash = false;
            for (char value : text)
            {
                const unsigned char character = static_cast<unsigned char>(value);
                if (std::isalnum(character) != 0)
                {
                    result.push_back(static_cast<char>(std::tolower(character)));
                    previousDash = false;
                    continue;
                }

                if (value == '-' || value == '_')
                {
                    if (!previousDash && !result.empty())
                    {
                        result.push_back('-');
                        previousDash = true;
                    }
                }
            }

            while (!result.empty() && result.back() == '-')
            {
                result.pop_back();
            }

            return result.empty() ? "vengine-project" : result;
        }

        [[nodiscard]] std::string MakeDefaultBundleIdentifier(const std::string& projectName)
        {
            return "com.vengine.packaged." + SanitizeBundleIdentifierSegment(projectName.empty() ? "VEngineProject" : projectName);
        }

        [[nodiscard]] std::string ReadEnvironmentString(const char* name, std::string fallback)
        {
            const char* value = std::getenv(name);
            return value != nullptr && value[0] != '\0' ? std::string(value) : std::move(fallback);
        }

        void FillDefaultBuildSettings(EditorProjectDescriptor& descriptor)
        {
            const std::string bundleIdentifier = MakeDefaultBundleIdentifier(descriptor.name);

            if (descriptor.buildSettings.mac.bundleIdentifier.empty())
            {
                descriptor.buildSettings.mac.bundleIdentifier = bundleIdentifier;
            }

            if (descriptor.buildSettings.mac.cmakeBuildConfig.empty())
            {
                descriptor.buildSettings.mac.cmakeBuildConfig = "Debug";
            }

            if (descriptor.buildSettings.ios.bundleIdentifier.empty())
            {
                descriptor.buildSettings.ios.bundleIdentifier = bundleIdentifier;
            }

            if (descriptor.buildSettings.ios.sdk.empty())
            {
                descriptor.buildSettings.ios.sdk = ReadEnvironmentString("VE_IOS_SDK", "iphoneos");
            }

            if (descriptor.buildSettings.ios.developmentTeam.empty())
            {
                descriptor.buildSettings.ios.developmentTeam = ReadEnvironmentString("VE_IOS_DEVELOPMENT_TEAM", "Auto");
            }

            if (descriptor.buildSettings.ios.codeSignStyle.empty())
            {
                descriptor.buildSettings.ios.codeSignStyle = ReadEnvironmentString("VE_IOS_CODE_SIGN_STYLE", "Automatic");
            }

            if (descriptor.buildSettings.ios.provisioningProfileSpecifier.empty())
            {
                descriptor.buildSettings.ios.provisioningProfileSpecifier =
                    ReadEnvironmentString("VE_IOS_PROVISIONING_PROFILE_SPECIFIER", "Automatic");
            }

            if (descriptor.buildSettings.ios.codeSignIdentity.empty())
            {
                descriptor.buildSettings.ios.codeSignIdentity = ReadEnvironmentString("VE_IOS_CODE_SIGN_IDENTITY", "Apple Development");
            }

            if (descriptor.buildSettings.ios.deploymentTarget.empty())
            {
                descriptor.buildSettings.ios.deploymentTarget = ReadEnvironmentString("VE_IOS_DEPLOYMENT_TARGET", "16.4");
            }

            if (descriptor.buildSettings.ios.exportMethod.empty())
            {
                descriptor.buildSettings.ios.exportMethod = ReadEnvironmentString("VE_IOS_EXPORT_METHOD", "development");
            }

            if (descriptor.buildSettings.ios.orientation.empty())
            {
                descriptor.buildSettings.ios.orientation = ReadEnvironmentString("VE_IOS_ORIENTATION", "Landscape");
            }

            if (descriptor.buildSettings.windows.configuration.empty())
            {
                descriptor.buildSettings.windows.configuration = "Release";
            }
        }

        [[nodiscard]] boost::json::object ToJson(const EditorProjectDescriptor::MacBuildSettings& settings)
        {
            boost::json::object object;
            object["bundleIdentifier"] = settings.bundleIdentifier;
            object["cmakeBuildConfig"] = settings.cmakeBuildConfig;
            return object;
        }

        [[nodiscard]] boost::json::object ToJson(const EditorProjectDescriptor::IOSBuildSettings& settings)
        {
            boost::json::object object;
            object["sdk"] = settings.sdk;
            object["bundleIdentifier"] = settings.bundleIdentifier;
            object["developmentTeam"] = settings.developmentTeam;
            object["codeSignStyle"] = settings.codeSignStyle;
            object["provisioningProfileSpecifier"] = settings.provisioningProfileSpecifier;
            object["codeSignIdentity"] = settings.codeSignIdentity;
            object["deploymentTarget"] = settings.deploymentTarget;
            object["exportMethod"] = settings.exportMethod;
            object["orientation"] = settings.orientation;
            return object;
        }

        [[nodiscard]] boost::json::object ToJson(const EditorProjectDescriptor::WindowsBuildSettings& settings)
        {
            boost::json::object object;
            object["configuration"] = settings.configuration;
            return object;
        }

        [[nodiscard]] boost::json::object ToJson(const EditorProjectDescriptor::BuildSettings& settings)
        {
            boost::json::object object;
            object["mac"] = ToJson(settings.mac);
            object["ios"] = ToJson(settings.ios);
            object["windows"] = ToJson(settings.windows);
            return object;
        }

        [[nodiscard]] EditorProjectDescriptor::MacBuildSettings ReadMacBuildSettings(const boost::json::object* object)
        {
            EditorProjectDescriptor::MacBuildSettings settings;
            if (object == nullptr)
            {
                return settings;
            }

            settings.bundleIdentifier = ReadString(*object, "bundleIdentifier");
            settings.cmakeBuildConfig = ReadString(*object, "cmakeBuildConfig", settings.cmakeBuildConfig);
            return settings;
        }

        [[nodiscard]] EditorProjectDescriptor::IOSBuildSettings ReadIOSBuildSettings(const boost::json::object* object)
        {
            EditorProjectDescriptor::IOSBuildSettings settings;
            if (object == nullptr)
            {
                return settings;
            }

            settings.bundleIdentifier = ReadString(*object, "bundleIdentifier");
            settings.sdk = ReadString(*object, "sdk", settings.sdk);
            settings.developmentTeam = ReadString(*object, "developmentTeam");
            settings.codeSignStyle = ReadString(*object, "codeSignStyle", settings.codeSignStyle);
            settings.provisioningProfileSpecifier = ReadString(*object, "provisioningProfileSpecifier");
            settings.codeSignIdentity = ReadString(*object, "codeSignIdentity");
            settings.deploymentTarget = ReadString(*object, "deploymentTarget", settings.deploymentTarget);
            settings.exportMethod = ReadString(*object, "exportMethod", settings.exportMethod);
            settings.orientation = ReadString(*object, "orientation", settings.orientation);
            return settings;
        }

        [[nodiscard]] EditorProjectDescriptor::WindowsBuildSettings ReadWindowsBuildSettings(const boost::json::object* object)
        {
            EditorProjectDescriptor::WindowsBuildSettings settings;
            if (object == nullptr)
            {
                return settings;
            }

            settings.configuration = ReadString(*object, "configuration", settings.configuration);
            return settings;
        }

        [[nodiscard]] EditorProjectDescriptor::BuildSettings ReadBuildSettings(const boost::json::object& object)
        {
            EditorProjectDescriptor::BuildSettings settings;
            const boost::json::object* buildSettingsObject = ReadObject(object, "buildSettings");
            if (buildSettingsObject == nullptr)
            {
                return settings;
            }

            settings.mac = ReadMacBuildSettings(ReadObject(*buildSettingsObject, "mac"));
            settings.ios = ReadIOSBuildSettings(ReadObject(*buildSettingsObject, "ios"));
            settings.windows = ReadWindowsBuildSettings(ReadObject(*buildSettingsObject, "windows"));
            return settings;
        }

        [[nodiscard]] const char* GetCurrentEngineVersion() noexcept
        {
            return GetBuildInfo().version;
        }

        [[nodiscard]] boost::json::object ToJson(const EditorProjectDescriptor& descriptor)
        {
            EditorProjectDescriptor savedDescriptor = descriptor;
            FillDefaultBuildSettings(savedDescriptor);

            boost::json::object object;
            object["schemaVersion"] = 1;
            object["name"] = savedDescriptor.name;
            object["engineVersion"] = savedDescriptor.engineVersion.empty() ? GetCurrentEngineVersion() : savedDescriptor.engineVersion;
            object["startScene"] = savedDescriptor.startScene;
            object["buildSettings"] = ToJson(savedDescriptor.buildSettings);
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
        descriptor.buildSettings = ReadBuildSettings(object);
        FillDefaultBuildSettings(descriptor);
        return Result<EditorProjectDescriptor>::Success(std::move(descriptor));
    }
} // namespace ve::editor
