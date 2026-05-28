#include "Engine/Runtime/Scripting/ScriptProject.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>

namespace ve
{
    namespace
    {
        using boost::json::object;
        using boost::json::value;

        constexpr std::string_view ProjectDescriptorFileName = ".veproject";
        constexpr std::string_view ProjectFormatName = "VEngine.Project";

#ifndef VE_DOTNET_EXECUTABLE
#define VE_DOTNET_EXECUTABLE "dotnet"
#endif

#ifndef VE_DOTNET_TARGET_FRAMEWORK
#define VE_DOTNET_TARGET_FRAMEWORK "net10.0"
#endif

        [[nodiscard]] Error MakeError(ErrorCode code, std::string message)
        {
            return Error(code, std::move(message));
        }

        [[nodiscard]] const value* FindMember(const object& jsonObject, const char* name)
        {
            const auto iter = jsonObject.find(name);
            return iter == jsonObject.end() ? nullptr : &iter->value();
        }

        [[nodiscard]] std::string ReadString(const object& jsonObject, const char* name)
        {
            const value* member = FindMember(jsonObject, name);
            return member != nullptr && member->is_string() ? std::string(member->as_string()) : std::string();
        }

        [[nodiscard]] Result<object> ReadJsonObject(const Path& path)
        {
            Result<std::string> textResult = FileSystem::ReadTextFile(path);
            if (!textResult)
            {
                return Result<object>::Failure(textResult.GetError());
            }

            boost::system::error_code parseError;
            value root = boost::json::parse(textResult.GetValue(), parseError);
            if (parseError || !root.is_object())
            {
                return Result<object>::Failure(
                    MakeError(ErrorCode::InvalidArgument, "Project descriptor root must be a JSON object."));
            }

            return Result<object>::Success(std::move(root.as_object()));
        }

        [[nodiscard]] bool IsGeneratedPath(const Path& path) noexcept
        {
            const std::string& text = path.GetString();
            return text == "Generated" || text.starts_with("Generated/");
        }

        [[nodiscard]] Result<WindowsScriptProjectConfig> ReadWindowsScriptConfig(const object& scriptingObject)
        {
            const value* windowsValue = FindMember(scriptingObject, "windows");
            if (windowsValue == nullptr)
            {
                return Result<WindowsScriptProjectConfig>::Success({});
            }

            if (!windowsValue->is_object())
            {
                return Result<WindowsScriptProjectConfig>::Failure(
                    MakeError(ErrorCode::InvalidArgument, "Project scripting.windows must be an object."));
            }

            const object& windowsObject = windowsValue->as_object();
            WindowsScriptProjectConfig config;
            config.projectPath = Path(ReadString(windowsObject, "project"));
            config.assemblyName = ReadString(windowsObject, "assemblyName");

            if (config.projectPath.IsEmpty() && config.assemblyName.empty())
            {
                return Result<WindowsScriptProjectConfig>::Success({});
            }

            if (config.projectPath.IsEmpty() || config.assemblyName.empty())
            {
                return Result<WindowsScriptProjectConfig>::Failure(
                    MakeError(ErrorCode::InvalidArgument,
                              "Project scripting.windows requires both project and assemblyName."));
            }

            if (config.projectPath.IsAbsolute() || IsGeneratedPath(config.projectPath))
            {
                return Result<WindowsScriptProjectConfig>::Failure(
                    MakeError(ErrorCode::InvalidArgument,
                              "Project scripting.windows.project must be an authored project-relative path."));
            }

            return Result<WindowsScriptProjectConfig>::Success(std::move(config));
        }

        [[nodiscard]] std::string QuoteCommandArgument(std::string_view argument)
        {
            std::string quoted = "\"";
            for (char value : argument)
            {
                if (value == '"')
                {
                    quoted += "\\\"";
                }
                else
                {
                    quoted.push_back(value);
                }
            }
            quoted.push_back('"');
            return quoted;
        }

        [[nodiscard]] ErrorCode RunCommand(const std::string& command)
        {
            const int result = std::system(command.c_str());
            return result == 0 ? ErrorCode::None : ErrorCode::PlatformError;
        }
    } // namespace

    const char* ToString(ScriptBuildConfiguration configuration) noexcept
    {
        switch (configuration)
        {
        case ScriptBuildConfiguration::Debug:
            return "Debug";
        case ScriptBuildConfiguration::Release:
            return "Release";
        default:
            return "Unknown";
        }
    }

    Result<ScriptBuildConfiguration> ParseScriptBuildConfiguration(std::string_view text)
    {
        if (text == "Debug")
        {
            return Result<ScriptBuildConfiguration>::Success(ScriptBuildConfiguration::Debug);
        }

        if (text == "Release")
        {
            return Result<ScriptBuildConfiguration>::Success(ScriptBuildConfiguration::Release);
        }

        return Result<ScriptBuildConfiguration>::Failure(
            MakeError(ErrorCode::InvalidArgument, "Unsupported script build configuration."));
    }

    Result<ScriptProjectConfig> LoadScriptProjectConfig(const Path& projectRoot)
    {
        const Path descriptorPath = projectRoot / ProjectDescriptorFileName;
        if (!FileSystem::IsFile(descriptorPath))
        {
            return Result<ScriptProjectConfig>::Failure(
                MakeError(ErrorCode::NotFound, "Project root does not contain .veproject."));
        }

        Result<object> descriptor = ReadJsonObject(descriptorPath);
        if (!descriptor)
        {
            return Result<ScriptProjectConfig>::Failure(descriptor.GetError());
        }

        const object& root = descriptor.GetValue();
        if (ReadString(root, "format") != ProjectFormatName)
        {
            return Result<ScriptProjectConfig>::Failure(
                MakeError(ErrorCode::InvalidArgument, "Unsupported project descriptor format."));
        }

        const value* scriptingValue = FindMember(root, "scripting");
        if (scriptingValue == nullptr)
        {
            return Result<ScriptProjectConfig>::Success({});
        }

        if (!scriptingValue->is_object())
        {
            return Result<ScriptProjectConfig>::Failure(
                MakeError(ErrorCode::InvalidArgument, "Project scripting section must be an object."));
        }

        Result<WindowsScriptProjectConfig> windowsConfig = ReadWindowsScriptConfig(scriptingValue->as_object());
        if (!windowsConfig)
        {
            return Result<ScriptProjectConfig>::Failure(windowsConfig.GetError());
        }

        ScriptProjectConfig config;
        config.windows = windowsConfig.MoveValue();
        return Result<ScriptProjectConfig>::Success(std::move(config));
    }

    Path GetWindowsScriptGeneratedOutputDirectory(const Path& projectRoot,
                                                  ScriptBuildConfiguration configuration,
                                                  std::string_view assemblyName)
    {
        return projectRoot / "Generated/Scripts/Windows" / ToString(configuration) / assemblyName;
    }

    Path GetWindowsScriptGeneratedIntermediateDirectory(const Path& projectRoot,
                                                        ScriptBuildConfiguration configuration,
                                                        std::string_view assemblyName)
    {
        return projectRoot / "Generated/Intermediates/Scripts/Windows" / ToString(configuration) / assemblyName;
    }

    Path GetWindowsPackagedScriptDirectory(const Path& contentRoot)
    {
        return contentRoot / "Scripts/Windows";
    }

    WindowsScriptBuildArtifacts GetWindowsScriptBuildArtifacts(const Path& outputDirectory,
                                                               std::string_view assemblyName)
    {
        WindowsScriptBuildArtifacts artifacts;
        artifacts.outputDirectory = outputDirectory;
        artifacts.projectAssemblyPath = outputDirectory / (std::string(assemblyName) + ".dll");
        artifacts.projectDepsJsonPath = outputDirectory / (std::string(assemblyName) + ".deps.json");
        artifacts.projectRuntimeConfigPath = outputDirectory / (std::string(assemblyName) + ".runtimeconfig.json");
        artifacts.projectPdbPath = outputDirectory / (std::string(assemblyName) + ".pdb");
        artifacts.scriptApiAssemblyPath = outputDirectory / "VEngine.ScriptAPI.dll";
        artifacts.scriptApiPdbPath = outputDirectory / "VEngine.ScriptAPI.pdb";
        return artifacts;
    }

    WindowsScriptBuildArtifacts GetWindowsGeneratedScriptBuildArtifacts(const Path& projectRoot,
                                                                        ScriptBuildConfiguration configuration,
                                                                        std::string_view assemblyName)
    {
        return GetWindowsScriptBuildArtifacts(
            GetWindowsScriptGeneratedOutputDirectory(projectRoot, configuration, assemblyName), assemblyName);
    }

    WindowsScriptBuildArtifacts GetWindowsPackagedScriptBuildArtifacts(const Path& contentRoot,
                                                                       std::string_view assemblyName)
    {
        return GetWindowsScriptBuildArtifacts(GetWindowsPackagedScriptDirectory(contentRoot), assemblyName);
    }

    ErrorCode EnsureWindowsScriptGeneratedDirectories(const Path& projectRoot,
                                                      ScriptBuildConfiguration configuration,
                                                      std::string_view assemblyName)
    {
        ErrorCode outputResult = FileSystem::CreateDirectories(
            GetWindowsScriptGeneratedOutputDirectory(projectRoot, configuration, assemblyName));
        if (outputResult != ErrorCode::None)
        {
            return outputResult;
        }

        return FileSystem::CreateDirectories(
            GetWindowsScriptGeneratedIntermediateDirectory(projectRoot, configuration, assemblyName));
    }

    ErrorCode ValidateWindowsScriptBuildArtifacts(const WindowsScriptBuildArtifacts& artifacts)
    {
        if (!FileSystem::IsFile(artifacts.projectAssemblyPath) ||
            !FileSystem::IsFile(artifacts.projectDepsJsonPath) ||
            !FileSystem::IsFile(artifacts.projectRuntimeConfigPath) ||
            !FileSystem::IsFile(artifacts.scriptApiAssemblyPath))
        {
            return ErrorCode::NotFound;
        }

        return ErrorCode::None;
    }

    Result<WindowsScriptBuildArtifacts> BuildWindowsScriptProject(const Path& projectRoot,
                                                                  const WindowsScriptProjectConfig& config,
                                                                  ScriptBuildConfiguration configuration)
    {
        if (!config.IsConfigured())
        {
            return Result<WindowsScriptBuildArtifacts>::Failure(
                MakeError(ErrorCode::InvalidArgument, "Windows scripting is not configured."));
        }

        const Path scriptProjectPath = projectRoot / config.projectPath;
        if (!FileSystem::IsFile(scriptProjectPath))
        {
            return Result<WindowsScriptBuildArtifacts>::Failure(
                MakeError(ErrorCode::NotFound, "Script project was not found: " + config.projectPath.GetString()));
        }

        ErrorCode directoryResult =
            EnsureWindowsScriptGeneratedDirectories(projectRoot, configuration, config.assemblyName);
        if (directoryResult != ErrorCode::None)
        {
            return Result<WindowsScriptBuildArtifacts>::Failure(
                MakeError(directoryResult, "Failed to create generated script output directories."));
        }

        const Path outputDirectory =
            GetWindowsScriptGeneratedOutputDirectory(projectRoot, configuration, config.assemblyName);
        const Path intermediateDirectory =
            GetWindowsScriptGeneratedIntermediateDirectory(projectRoot, configuration, config.assemblyName);

#if VE_PLATFORM_WINDOWS
        std::ostringstream commandBody;
        const std::string intermediatePath = intermediateDirectory.GetString() + "/";
        commandBody << QuoteCommandArgument(VE_DOTNET_EXECUTABLE) << " build "
                    << QuoteCommandArgument(scriptProjectPath.GetString()) << " --configuration "
                    << QuoteCommandArgument(ToString(configuration)) << " --framework "
                    << QuoteCommandArgument(VE_DOTNET_TARGET_FRAMEWORK) << " --output "
                    << QuoteCommandArgument(outputDirectory.GetString()) << " -p:BaseIntermediateOutputPath="
                    << QuoteCommandArgument(intermediatePath);

        const ErrorCode buildResult = RunCommand("\"" + commandBody.str() + "\"");
        if (buildResult != ErrorCode::None)
        {
            return Result<WindowsScriptBuildArtifacts>::Failure(
                MakeError(buildResult, "dotnet build failed for script project: " + config.projectPath.GetString()));
        }
#else
        return Result<WindowsScriptBuildArtifacts>::Failure(
            MakeError(ErrorCode::Unsupported, "Windows C# script builds are supported only on Windows."));
#endif

        WindowsScriptBuildArtifacts artifacts = GetWindowsScriptBuildArtifacts(outputDirectory, config.assemblyName);
        const ErrorCode validateResult = ValidateWindowsScriptBuildArtifacts(artifacts);
        if (validateResult != ErrorCode::None)
        {
            return Result<WindowsScriptBuildArtifacts>::Failure(
                MakeError(validateResult, "Script build output is missing required runtime files."));
        }

        return Result<WindowsScriptBuildArtifacts>::Success(std::move(artifacts));
    }
} // namespace ve
