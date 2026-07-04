#include "Editor/Core/EditorProjectPacker.h"

#include "Editor/Core/Editor.h"
#include "Editor/Core/EditorAssetDatabase.h"
#include "Editor/Core/EditorAssetPath.h"
#include "Editor/Core/EditorProject.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Resource/AssetManifest.h"

#include <algorithm>
#include <boost/json.hpp>
#include <cctype>
#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace ve::editor
{
    namespace
    {
        constexpr const char* PackageManifestFilename = "AssetManifest.json";
        constexpr const char* ScriptAssemblyManifestFilename = "ScriptAssembly.json";

        [[nodiscard]] std::string MakeErrorMessage(const char* operation, const Path& path, const std::error_code& errorCode)
        {
            return std::string(operation) + " failed for '" + path.GetString() + "': " + errorCode.message();
        }

        [[nodiscard]] std::string ReadString(const boost::json::object& object, boost::json::string_view key, std::string fallback = {})
        {
            if (const boost::json::value* value = object.if_contains(key); value != nullptr && value->is_string())
            {
                return std::string(value->as_string());
            }

            return fallback;
        }

        [[nodiscard]] Result<std::vector<Path>> CollectShaderArtifactPaths(const Path& shaderDescriptorPath)
        {
            Result<std::string> text = FileSystem::ReadTextFile(shaderDescriptorPath);
            if (!text)
            {
                return Result<std::vector<Path>>::Failure(text.GetError());
            }

            Result<boost::json::value> json = JsonUtils::Parse(text.GetValue());
            if (!json || !json.GetValue().is_object())
            {
                return Result<std::vector<Path>>::Failure(Error(ErrorCode::InvalidArgument, "Shader descriptor root must be a JSON object."));
            }

            const boost::json::value* stagesValue = json.GetValue().as_object().if_contains("stages");
            if (stagesValue == nullptr || !stagesValue->is_array())
            {
                return Result<std::vector<Path>>::Failure(Error(ErrorCode::InvalidArgument, "Shader descriptor missing stages array."));
            }

            std::vector<Path> paths;
            for (const boost::json::value& stageValue : stagesValue->as_array())
            {
                if (!stageValue.is_object())
                {
                    continue;
                }

                const boost::json::value* artifactsValue = stageValue.as_object().if_contains("artifacts");
                if (artifactsValue == nullptr || !artifactsValue->is_object())
                {
                    continue;
                }

                const boost::json::object& artifacts = artifactsValue->as_object();
                const auto collectArtifactPath = [&artifacts, &paths](const char* key)
                {
                    const std::string artifactPath = ReadString(artifacts, key);
                    if (!artifactPath.empty())
                    {
                        paths.emplace_back(artifactPath);
                    }
                };
#if VE_PLATFORM_WINDOWS
                collectArtifactPath("d3d11");
                collectArtifactPath("d3d12");
#elif VE_PLATFORM_MACOS
                collectArtifactPath("metal");
#endif
            }

            return Result<std::vector<Path>>::Success(std::move(paths));
        }
    } // namespace

    EditorProjectPacker::~EditorProjectPacker()
    {
        CloseLogFile();
    }

    void EditorProjectPacker::Reset()
    {
        CloseLogFile();
        status_ = PackageRunStatus::Idle;
        steps_.clear();
        currentStepIndex_ = 0;
        statusMessage_.clear();
        timestamp_.clear();
        projectName_.clear();
        buildSettings_ = EditorProjectDescriptor::BuildSettings{};
        projectRoot_ = Path();
        buildRoot_ = Path();
        logDirectory_ = Path();
        logPath_ = Path();
        outputRoot_ = Path();
        packageBinRoot_ = Path();
        packageDataRoot_ = Path();
        packageRuntimeLogRoot_ = Path();
        logPathText_.clear();
        outputPathText_.clear();
        ResetPlatformState();
    }

    void EditorProjectPacker::Start(Editor& editor, const EditorProjectDescriptor::BuildSettings& buildSettings)
    {
        Reset();

        if (editor.GetCurrentProjectPath().empty())
        {
            status_ = PackageRunStatus::Failed;
            statusMessage_ = "No project is open.";
            return;
        }

        timestamp_ = MakeTimestamp();
        projectName_ = editor.GetCurrentProjectName().empty() ? Editor::GetProjectDisplayName(editor.GetCurrentProjectPath()) : editor.GetCurrentProjectName();
        buildSettings_ = buildSettings;
        projectRoot_ = Path(editor.GetCurrentProjectPath());
        buildRoot_ = projectRoot_ / "Build";
        logDirectory_ = buildRoot_ / "Logs";

        ConfigurePackagePaths();
        logPathText_ = PathToString(logPath_);
        outputPathText_ = PathToString(outputRoot_);

        InitializeSteps();
        OpenLogFile();
        if (status_ == PackageRunStatus::Failed)
        {
            return;
        }

        status_ = PackageRunStatus::Running;
        statusMessage_ = GetRunningStatusMessage();
        LogLine(std::string("Started ") + GetPlatformName() + " packaging.");
        LogLine("Project: " + projectRoot_.GetString());
        LogLine("Output: " + outputRoot_.GetString());
    }

    void EditorProjectPacker::Advance(Editor& editor)
    {
        if (status_ != PackageRunStatus::Running || currentStepIndex_ >= steps_.size())
        {
            return;
        }

        PackageStepState& step = steps_[currentStepIndex_];
        step.status = PackageStepStatus::Running;
        step.message = "Running";
        statusMessage_ = step.name;
        LogLine("Step started: " + step.name);

        ErrorCode result = ErrorCode::Unknown;
        try
        {
            result = RunStep(currentStepIndex_, editor);
        }
        catch (const std::filesystem::filesystem_error& exception)
        {
            FailCurrentStep(ErrorCode::IOError, exception.what());
            return;
        }
        catch (const std::exception& exception)
        {
            FailCurrentStep(ErrorCode::Unknown, exception.what());
            return;
        }

        if (result != ErrorCode::None)
        {
            FailCurrentStep(result, std::string("Step failed: ") + ToString(result));
            return;
        }

        CompleteCurrentStep("Done");
        ++currentStepIndex_;
        if (currentStepIndex_ >= steps_.size())
        {
            status_ = PackageRunStatus::Succeeded;
            statusMessage_ = GetSucceededStatusMessage();
            LogLine("Packaging completed successfully.");
            CloseLogFile();
        }
    }

    PackageRunStatus EditorProjectPacker::GetStatus() const noexcept
    {
        return status_;
    }

    float EditorProjectPacker::GetProgress() const noexcept
    {
        if (steps_.empty())
        {
            return 0.0F;
        }

        const size_t completedCount = (std::min)(currentStepIndex_, steps_.size());
        return static_cast<float>(completedCount) / static_cast<float>(steps_.size());
    }

    const std::vector<PackageStepState>& EditorProjectPacker::GetSteps() const noexcept
    {
        return steps_;
    }

    const std::string& EditorProjectPacker::GetStatusMessage() const noexcept
    {
        return statusMessage_;
    }

    const std::string& EditorProjectPacker::GetLogPath() const noexcept
    {
        return logPathText_;
    }

    const std::string& EditorProjectPacker::GetOutputPath() const noexcept
    {
        return outputPathText_;
    }

    void EditorProjectPacker::ResetPlatformState() {}

    ErrorCode EditorProjectPacker::PreparePackageDirectories()
    {
        ErrorCode result = CreateDirectory(logDirectory_);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = CreateDirectory(packageBinRoot_);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = CreateDirectory(packageDataRoot_);
        if (result != ErrorCode::None)
        {
            return result;
        }

        if (!packageRuntimeLogRoot_.IsEmpty())
        {
            result = CreateDirectory(packageRuntimeLogRoot_);
            if (result != ErrorCode::None)
            {
                return result;
            }
        }

        const Path sourceDescriptor = EditorProject::GetDescriptorPath(projectRoot_);
        const Path destinationDescriptor = packageDataRoot_ / EditorProject::DescriptorFilename;
        result = CopyFileWithDirectories(sourceDescriptor, destinationDescriptor);
        if (result != ErrorCode::None)
        {
            return result;
        }

        LogLine("Copied project descriptor: " + sourceDescriptor.GetString() + " -> " + destinationDescriptor.GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPacker::RefreshAssetDatabase(Editor& editor)
    {
        const ErrorCode result = editor.GetAssetDatabase().Refresh();
        if (result != ErrorCode::None)
        {
            LogError(result, "Failed to refresh asset database.");
            return result;
        }

        LogLine("Asset database refreshed. Asset count: " + std::to_string(editor.GetAssetDatabase().GetAssetCount()));
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPacker::ExportAssetManifest(Editor& editor)
    {
        AssetManifest manifest;
        for (const auto& pair : editor.GetAssetDatabase().GetAssetsByID())
        {
            ManifestAssetRecord manifestRecord;
            manifestRecord.asset = pair.second.asset;
            manifestRecord.bundle = "Main";

            const ErrorCode addResult = manifest.AddOrUpdate(std::move(manifestRecord));
            if (addResult != ErrorCode::None)
            {
                LogError(addResult, "Failed to add asset to package manifest.");
                return addResult;
            }
        }

        const Path manifestPath = packageDataRoot_ / PackageManifestFilename;
        const ErrorCode saveResult = manifest.SaveToFile(manifestPath);
        if (saveResult != ErrorCode::None)
        {
            LogError(saveResult, "Failed to write asset manifest: " + manifestPath.GetString());
            return saveResult;
        }

        LogLine("Wrote asset manifest: " + manifestPath.GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPacker::CopyRuntimeAssets(Editor& editor)
    {
        size_t copiedCount = 0;
        for (const auto& pair : editor.GetAssetDatabase().GetAssetsByID())
        {
            const Path& runtimePath = pair.second.asset.runtimePath;
            if (runtimePath.IsEmpty())
            {
                continue;
            }

            const Path sourcePath = ResolveEditorContentPath(projectRoot_, runtimePath);
            const Path destinationPath = packageDataRoot_ / runtimePath;
            ErrorCode result = CopyFileWithDirectories(sourcePath, destinationPath);
            if (result != ErrorCode::None)
            {
                return result;
            }

            ++copiedCount;
            LogLine("Copied runtime asset: " + sourcePath.GetString() + " -> " + destinationPath.GetString());

            if (pair.second.asset.type != ResourceType::Shader)
            {
                continue;
            }

            Result<std::vector<Path>> artifactPaths = CollectShaderArtifactPaths(sourcePath);
            if (!artifactPaths)
            {
                LogError(artifactPaths.GetError().GetCode(), "Failed to read shader artifact list: " + sourcePath.GetString());
                return artifactPaths.GetError().GetCode();
            }

            for (const Path& artifactRuntimePath : artifactPaths.GetValue())
            {
                if (artifactRuntimePath.IsEmpty() || artifactRuntimePath.IsAbsolute())
                {
                    LogError(ErrorCode::InvalidArgument, "Shader artifact path must be project-relative: " + artifactRuntimePath.GetString());
                    return ErrorCode::InvalidArgument;
                }

                const Path artifactSourcePath = ResolveEditorContentPath(projectRoot_, artifactRuntimePath);
                const Path artifactDestinationPath = packageDataRoot_ / artifactRuntimePath;
                result = CopyFileWithDirectories(artifactSourcePath, artifactDestinationPath);
                if (result != ErrorCode::None)
                {
                    return result;
                }

                ++copiedCount;
                LogLine("Copied shader artifact: " + artifactSourcePath.GetString() + " -> " + artifactDestinationPath.GetString());
            }
        }

        LogLine("Runtime asset copy completed. File count: " + std::to_string(copiedCount));
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPacker::CopyManagedScripts()
    {
        const Path sourceRoot = projectRoot_ / "Library" / "Scripting" / "output";
        if (!FileSystem::IsDirectory(sourceRoot))
        {
            LogLine("No compiled script output found. Skipping managed script copy: " + sourceRoot.GetString());
            return ErrorCode::None;
        }

        const Path sourceAssembly = sourceRoot / (projectName_ + ".Scripts.dll");
        if (!FileSystem::IsFile(sourceAssembly))
        {
            LogLine("No compiled project script assembly found. Skipping managed script copy: " + sourceAssembly.GetString());
            return ErrorCode::None;
        }

        const Path destinationRoot = packageDataRoot_ / "Scripts";
        ErrorCode result = CreateDirectory(destinationRoot);
        if (result != ErrorCode::None)
        {
            return result;
        }

        size_t copiedFileCount = 0;
        std::error_code errorCode;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(ToNativePath(sourceRoot), errorCode))
        {
            if (errorCode)
            {
                LogError(ErrorCode::IOError, MakeErrorMessage("List directory", sourceRoot, errorCode));
                return ErrorCode::IOError;
            }

            if (!entry.is_regular_file(errorCode))
            {
                errorCode.clear();
                continue;
            }

            const Path sourceFile(entry.path().generic_string());
            const Path destinationFile = destinationRoot / entry.path().filename().generic_string();
            result = CopyFileWithDirectories(sourceFile, destinationFile);
            if (result != ErrorCode::None)
            {
                return result;
            }

            ++copiedFileCount;
        }

        const Path sourceManifest = sourceRoot / ScriptAssemblyManifestFilename;
        if (FileSystem::IsFile(sourceManifest))
        {
            Result<std::string> text = FileSystem::ReadTextFile(sourceManifest);
            if (!text)
            {
                LogError(text.GetError().GetCode(), "Failed to read script manifest: " + sourceManifest.GetString());
                return text.GetError().GetCode();
            }

            Result<boost::json::value> json = JsonUtils::Parse(text.GetValue());
            if (!json || !json.GetValue().is_object())
            {
                LogError(ErrorCode::InvalidArgument, "Script manifest root must be a JSON object: " + sourceManifest.GetString());
                return ErrorCode::InvalidArgument;
            }

            boost::json::object manifest = json.GetValue().as_object();
            manifest["assemblyPath"] = std::string("Scripts/") + projectName_ + ".Scripts.dll";
            const ErrorCode writeResult = FileSystem::WriteTextFile(destinationRoot / ScriptAssemblyManifestFilename, JsonUtils::SerializePretty(manifest));
            if (writeResult != ErrorCode::None)
            {
                LogError(writeResult, "Failed to write packaged script manifest.");
                return writeResult;
            }
        }

        LogLine("Copied managed scripts: " + sourceRoot.GetString() + " -> " + destinationRoot.GetString() +
                ". File count: " + std::to_string(copiedFileCount));
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPacker::WritePackageInfo(const PackageInfoDesc& desc)
    {
        boost::json::object object;
        object["schemaVersion"] = 1;
        object["platform"] = desc.platform;
        object["projectName"] = projectName_;
        object["projectRoot"] = projectRoot_.GetString();
        object["outputPath"] = outputRoot_.GetString();
        object["generatedAt"] = timestamp_;
        object["playerExecutable"] = desc.playerExecutable;
        object["dataRoot"] = desc.dataRoot;
        object["assetManifest"] = desc.dataRoot + "/" + PackageManifestFilename;
        object["packagingLog"] = logPath_.GetString();
        if (!desc.appBundle.empty())
        {
            object["appBundle"] = desc.appBundle;
        }

        const ErrorCode writeResult = FileSystem::WriteTextFile(desc.packageInfoPath, JsonUtils::SerializePretty(object));
        if (writeResult != ErrorCode::None)
        {
            LogError(writeResult, "Failed to write package info: " + desc.packageInfoPath.GetString());
            return writeResult;
        }

        LogLine("Wrote package info: " + desc.packageInfoPath.GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPacker::CreateDirectory(const Path& path)
    {
        std::error_code errorCode;
        std::filesystem::create_directories(ToNativePath(path), errorCode);
        if (errorCode)
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("Create directory", path, errorCode));
            return ErrorCode::IOError;
        }

        return ErrorCode::None;
    }

    ErrorCode EditorProjectPacker::CopyFileWithDirectories(const Path& sourcePath, const Path& destinationPath)
    {
        const std::filesystem::path nativeSource = ToNativePath(sourcePath);
        const std::filesystem::path nativeDestination = ToNativePath(destinationPath);

        std::error_code errorCode;
        std::filesystem::create_directories(nativeDestination.parent_path(), errorCode);
        if (errorCode)
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("Create directory", Path(nativeDestination.parent_path().generic_string()), errorCode));
            return ErrorCode::IOError;
        }

        std::filesystem::copy_file(nativeSource, nativeDestination, std::filesystem::copy_options::overwrite_existing, errorCode);
        if (errorCode)
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("Copy file", sourcePath, errorCode));
            return ErrorCode::IOError;
        }

        return ErrorCode::None;
    }

    ErrorCode EditorProjectPacker::CopyDirectory(const Path& sourcePath, const Path& destinationPath)
    {
        const std::filesystem::path nativeSource = ToNativePath(sourcePath);
        const std::filesystem::path nativeDestination = ToNativePath(destinationPath);

        std::error_code errorCode;
        if (!std::filesystem::is_directory(nativeSource, errorCode))
        {
            LogError(ErrorCode::NotFound, "Directory was not found: " + sourcePath.GetString());
            return ErrorCode::NotFound;
        }

        std::filesystem::create_directories(nativeDestination, errorCode);
        if (errorCode)
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("Create directory", destinationPath, errorCode));
            return ErrorCode::IOError;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(nativeSource, errorCode))
        {
            if (errorCode)
            {
                LogError(ErrorCode::IOError, MakeErrorMessage("Copy directory", sourcePath, errorCode));
                return ErrorCode::IOError;
            }

            const std::filesystem::path relativePath = std::filesystem::relative(entry.path(), nativeSource, errorCode);
            if (errorCode)
            {
                LogError(ErrorCode::IOError, MakeErrorMessage("Resolve relative path", sourcePath, errorCode));
                return ErrorCode::IOError;
            }

            const std::filesystem::path targetPath = nativeDestination / relativePath;
            if (entry.is_directory(errorCode))
            {
                std::filesystem::create_directories(targetPath, errorCode);
                if (errorCode)
                {
                    LogError(ErrorCode::IOError, MakeErrorMessage("Create directory", Path(targetPath.generic_string()), errorCode));
                    return ErrorCode::IOError;
                }

                continue;
            }

            if (!entry.is_regular_file(errorCode))
            {
                errorCode.clear();
                continue;
            }

            std::filesystem::create_directories(targetPath.parent_path(), errorCode);
            if (errorCode)
            {
                LogError(ErrorCode::IOError, MakeErrorMessage("Create directory", Path(targetPath.parent_path().generic_string()), errorCode));
                return ErrorCode::IOError;
            }

            std::filesystem::copy_file(entry.path(), targetPath, std::filesystem::copy_options::overwrite_existing, errorCode);
            if (errorCode)
            {
                LogError(ErrorCode::IOError, MakeErrorMessage("Copy file", Path(entry.path().generic_string()), errorCode));
                return ErrorCode::IOError;
            }
        }

        if (errorCode)
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("Copy directory", sourcePath, errorCode));
            return ErrorCode::IOError;
        }

        return ErrorCode::None;
    }

    void EditorProjectPacker::CompleteCurrentStep(std::string message)
    {
        PackageStepState& step = steps_[currentStepIndex_];
        step.status = PackageStepStatus::Succeeded;
        step.message = std::move(message);
        LogLine("Step completed: " + step.name);
    }

    void EditorProjectPacker::FailCurrentStep(ErrorCode code, std::string message)
    {
        PackageStepState& step = steps_[currentStepIndex_];
        step.status = PackageStepStatus::Failed;
        step.message = message;
        status_ = PackageRunStatus::Failed;
        statusMessage_ = "Packaging failed: " + message;
        LogError(code, "Step failed: " + step.name + ". " + message);
        LogLine("Packaging failed.");
        CloseLogFile();
    }

    void EditorProjectPacker::OpenLogFile()
    {
        std::error_code errorCode;
        std::filesystem::create_directories(ToNativePath(logDirectory_), errorCode);
        if (errorCode)
        {
            status_ = PackageRunStatus::Failed;
            statusMessage_ = MakeErrorMessage("Create directory", logDirectory_, errorCode);
            return;
        }

        logFile_.open(ToNativePath(logPath_), std::ios::out | std::ios::trunc);
        if (!logFile_)
        {
            status_ = PackageRunStatus::Failed;
            statusMessage_ = "Failed to open packaging log: " + logPath_.GetString();
        }
    }

    void EditorProjectPacker::CloseLogFile()
    {
        if (logFile_.is_open())
        {
            logFile_.flush();
            logFile_.close();
        }
    }

    void EditorProjectPacker::LogLine(const std::string& message)
    {
        if (logFile_.is_open())
        {
            logFile_ << message << '\n';
            logFile_.flush();
        }
    }

    void EditorProjectPacker::LogError(ErrorCode code, const std::string& message)
    {
        LogLine(std::string("[Error] ") + ToString(code) + ": " + message);
    }

    std::string EditorProjectPacker::MakeTimestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t timeValue = std::chrono::system_clock::to_time_t(now);
        std::tm localTime = {};
#if defined(_WIN32)
        localtime_s(&localTime, &timeValue);
#else
        localtime_r(&timeValue, &localTime);
#endif

        std::ostringstream stream;
        stream << std::put_time(&localTime, "%Y%m%d_%H%M%S");
        return stream.str();
    }

    std::string EditorProjectPacker::MakePackageDirectoryName(const std::string& projectName, const std::string& timestamp)
    {
        return SanitizePathSegment(projectName.empty() ? "VEngineProject" : projectName) + "_" + timestamp;
    }

    std::string EditorProjectPacker::SanitizePathSegment(std::string text)
    {
        for (char& value : text)
        {
            const unsigned char character = static_cast<unsigned char>(value);
            if (std::isalnum(character) == 0 && value != '-' && value != '_')
            {
                value = '_';
            }
        }

        text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());
        if (text.empty())
        {
            return "VEngineProject";
        }

        return text;
    }

    std::string EditorProjectPacker::PathToString(const Path& path)
    {
        return path.GetString();
    }

    std::filesystem::path EditorProjectPacker::ToNativePath(const Path& path)
    {
        return std::filesystem::path(path.GetString());
    }
} // namespace ve::editor
