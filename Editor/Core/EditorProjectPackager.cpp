#include "Editor/Core/EditorProjectPackager.h"

#include "Editor/Core/Editor.h"
#include "Editor/Core/EditorAssetDatabase.h"
#include "Editor/Core/EditorProject.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Resource/AssetManifest.h"

#include <algorithm>
#include <boost/json.hpp>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace ve::editor
{
    namespace
    {
        constexpr const char* WindowsPlatformName = "Windows";
        constexpr const char* PlayerExecutableName = "VEnginePlayer.exe";
        constexpr const char* PackageManifestFilename = "AssetManifest.json";
        constexpr const char* PackageInfoFilename = "PackageInfo.json";

        [[nodiscard]] std::filesystem::path ToNativePath(const Path& path)
        {
            return std::filesystem::path(path.GetString());
        }

        [[nodiscard]] bool CopyFileWithDirectories(const Path& sourcePath, const Path& destinationPath, std::error_code& errorCode)
        {
            const std::filesystem::path nativeSource = ToNativePath(sourcePath);
            const std::filesystem::path nativeDestination = ToNativePath(destinationPath);
            std::filesystem::create_directories(nativeDestination.parent_path(), errorCode);
            if (errorCode)
            {
                return false;
            }

            std::filesystem::copy_file(nativeSource, nativeDestination, std::filesystem::copy_options::overwrite_existing, errorCode);
            return !errorCode;
        }

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
                for (const char* key : {"d3d11", "d3d12", "spirv", "metal", "reflection"})
                {
                    const std::string artifactPath = ReadString(artifacts, key);
                    if (!artifactPath.empty())
                    {
                        paths.emplace_back(artifactPath);
                    }
                }
            }

            return Result<std::vector<Path>>::Success(std::move(paths));
        }
    } // namespace

    EditorProjectPackager::~EditorProjectPackager()
    {
        CloseLogFile();
    }

    void EditorProjectPackager::Reset()
    {
        CloseLogFile();
        status_ = PackageRunStatus::Idle;
        steps_.clear();
        currentStepIndex_ = 0;
        statusMessage_.clear();
        timestamp_.clear();
        projectName_.clear();
        projectRoot_ = Path();
        buildRoot_ = Path();
        logDirectory_ = Path();
        logPath_ = Path();
        outputRoot_ = Path();
        packageBinRoot_ = Path();
        packageDataRoot_ = Path();
        logPathText_.clear();
        outputPathText_.clear();
    }

    void EditorProjectPackager::StartWindowsPackage(Editor& editor)
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
        projectRoot_ = Path(editor.GetCurrentProjectPath());
        buildRoot_ = projectRoot_ / "Build";
        logDirectory_ = buildRoot_ / "Logs";
        logPath_ = logDirectory_ / ("Package_Windows_" + timestamp_ + ".log");
        outputRoot_ = buildRoot_ / WindowsPlatformName / MakePackageDirectoryName(projectName_, timestamp_);
        packageBinRoot_ = outputRoot_ / "Bin";
        packageDataRoot_ = outputRoot_ / "Data";
        logPathText_ = PathToString(logPath_);
        outputPathText_ = PathToString(outputRoot_);

        InitializeSteps();
        OpenLogFile();
        if (status_ == PackageRunStatus::Failed)
        {
            return;
        }

        status_ = PackageRunStatus::Running;
        statusMessage_ = "Packaging Windows build.";
        LogLine("Started Windows packaging.");
        LogLine("Project: " + projectRoot_.GetString());
        LogLine("Output: " + outputRoot_.GetString());
    }

    void EditorProjectPackager::Advance(Editor& editor)
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
            switch (currentStepIndex_)
            {
            case 0:
                result = PrepareDirectories();
                break;
            case 1:
                result = RefreshAssetDatabase(editor);
                break;
            case 2:
                result = ExportAssetManifest(editor);
                break;
            case 3:
                result = CopyRuntimeAssets(editor);
                break;
            case 4:
                result = CopyWindowsPlayerExecutable();
                break;
            case 5:
                result = WritePackageInfo();
                break;
            default:
                result = ErrorCode::InvalidState;
                break;
            }
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
            statusMessage_ = "Windows package completed.";
            LogLine("Packaging completed successfully.");
            CloseLogFile();
        }
    }

    PackageRunStatus EditorProjectPackager::GetStatus() const noexcept
    {
        return status_;
    }

    float EditorProjectPackager::GetProgress() const noexcept
    {
        if (steps_.empty())
        {
            return 0.0F;
        }

        const size_t completedCount = (std::min)(currentStepIndex_, steps_.size());
        return static_cast<float>(completedCount) / static_cast<float>(steps_.size());
    }

    const std::vector<PackageStepState>& EditorProjectPackager::GetSteps() const noexcept
    {
        return steps_;
    }

    const std::string& EditorProjectPackager::GetStatusMessage() const noexcept
    {
        return statusMessage_;
    }

    const std::string& EditorProjectPackager::GetLogPath() const noexcept
    {
        return logPathText_;
    }

    const std::string& EditorProjectPackager::GetOutputPath() const noexcept
    {
        return outputPathText_;
    }

    ErrorCode EditorProjectPackager::PrepareDirectories()
    {
        std::error_code errorCode;
        std::filesystem::create_directories(ToNativePath(logDirectory_), errorCode);
        if (errorCode)
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("Create directory", logDirectory_, errorCode));
            return ErrorCode::IOError;
        }

        std::filesystem::create_directories(ToNativePath(packageBinRoot_), errorCode);
        if (errorCode)
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("Create directory", packageBinRoot_, errorCode));
            return ErrorCode::IOError;
        }

        std::filesystem::create_directories(ToNativePath(packageDataRoot_), errorCode);
        if (errorCode)
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("Create directory", packageDataRoot_, errorCode));
            return ErrorCode::IOError;
        }

        const Path packageRuntimeLogRoot = outputRoot_ / "Logs";
        std::filesystem::create_directories(ToNativePath(packageRuntimeLogRoot), errorCode);
        if (errorCode)
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("Create directory", packageRuntimeLogRoot, errorCode));
            return ErrorCode::IOError;
        }

        const Path sourceDescriptor = EditorProject::GetDescriptorPath(projectRoot_);
        const Path destinationDescriptor = packageDataRoot_ / EditorProject::DescriptorFilename;
        if (!CopyFileWithDirectories(sourceDescriptor, destinationDescriptor, errorCode))
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("Copy file", sourceDescriptor, errorCode));
            return ErrorCode::IOError;
        }
        LogLine("Copied project descriptor: " + sourceDescriptor.GetString() + " -> " + destinationDescriptor.GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackager::RefreshAssetDatabase(Editor& editor)
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

    ErrorCode EditorProjectPackager::ExportAssetManifest(Editor& editor)
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

    ErrorCode EditorProjectPackager::CopyRuntimeAssets(Editor& editor)
    {
        size_t copiedCount = 0;
        for (const auto& pair : editor.GetAssetDatabase().GetAssetsByID())
        {
            const Path& runtimePath = pair.second.asset.runtimePath;
            if (runtimePath.IsEmpty())
            {
                continue;
            }

            const Path sourcePath = projectRoot_ / runtimePath;
            const Path destinationPath = packageDataRoot_ / runtimePath;
            std::error_code errorCode;
            if (!CopyFileWithDirectories(sourcePath, destinationPath, errorCode))
            {
                LogError(ErrorCode::IOError, MakeErrorMessage("Copy file", sourcePath, errorCode));
                return ErrorCode::IOError;
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

                const Path artifactSourcePath = projectRoot_ / artifactRuntimePath;
                const Path artifactDestinationPath = packageDataRoot_ / artifactRuntimePath;
                if (!CopyFileWithDirectories(artifactSourcePath, artifactDestinationPath, errorCode))
                {
                    LogError(ErrorCode::IOError, MakeErrorMessage("Copy file", artifactSourcePath, errorCode));
                    return ErrorCode::IOError;
                }

                ++copiedCount;
                LogLine("Copied shader artifact: " + artifactSourcePath.GetString() + " -> " + artifactDestinationPath.GetString());
            }
        }

        LogLine("Runtime asset copy completed. File count: " + std::to_string(copiedCount));
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackager::CopyWindowsPlayerExecutable()
    {
        const Path playerSourcePath = FileSystem::GetExecutableDirectory() / PlayerExecutableName;
        const Path playerDestinationPath = packageBinRoot_ / PlayerExecutableName;

        if (!FileSystem::IsFile(playerSourcePath))
        {
            LogError(ErrorCode::NotFound, "Windows player executable was not found: " + playerSourcePath.GetString());
            return ErrorCode::NotFound;
        }

        std::error_code errorCode;
        if (!CopyFileWithDirectories(playerSourcePath, playerDestinationPath, errorCode))
        {
            LogError(ErrorCode::IOError, MakeErrorMessage("Copy file", playerSourcePath, errorCode));
            return ErrorCode::IOError;
        }

        LogLine("Copied Windows player: " + playerSourcePath.GetString() + " -> " + playerDestinationPath.GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPackager::WritePackageInfo()
    {
        boost::json::object object;
        object["schemaVersion"] = 1;
        object["platform"] = WindowsPlatformName;
        object["projectName"] = projectName_;
        object["projectRoot"] = projectRoot_.GetString();
        object["outputPath"] = outputRoot_.GetString();
        object["generatedAt"] = timestamp_;
        object["playerExecutable"] = std::string("Bin/") + PlayerExecutableName;
        object["dataRoot"] = "Data";
        object["assetManifest"] = std::string("Data/") + PackageManifestFilename;
        object["packagingLog"] = logPath_.GetString();

        const Path packageInfoPath = outputRoot_ / PackageInfoFilename;
        const ErrorCode writeResult = FileSystem::WriteTextFile(packageInfoPath, JsonUtils::SerializePretty(object));
        if (writeResult != ErrorCode::None)
        {
            LogError(writeResult, "Failed to write package info: " + packageInfoPath.GetString());
            return writeResult;
        }

        LogLine("Wrote package info: " + packageInfoPath.GetString());
        return ErrorCode::None;
    }

    void EditorProjectPackager::InitializeSteps()
    {
        steps_ = {
            PackageStepState{.name = "Prepare package directories"},
            PackageStepState{.name = "Refresh asset database"},
            PackageStepState{.name = "Export asset manifest"},
            PackageStepState{.name = "Copy runtime assets"},
            PackageStepState{.name = "Copy Windows player"},
            PackageStepState{.name = "Write package info"},
        };
    }

    void EditorProjectPackager::CompleteCurrentStep(std::string message)
    {
        PackageStepState& step = steps_[currentStepIndex_];
        step.status = PackageStepStatus::Succeeded;
        step.message = std::move(message);
        LogLine("Step completed: " + step.name);
    }

    void EditorProjectPackager::FailCurrentStep(ErrorCode code, std::string message)
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

    void EditorProjectPackager::OpenLogFile()
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

    void EditorProjectPackager::CloseLogFile()
    {
        if (logFile_.is_open())
        {
            logFile_.flush();
            logFile_.close();
        }
    }

    void EditorProjectPackager::LogLine(const std::string& message)
    {
        if (logFile_.is_open())
        {
            logFile_ << message << '\n';
            logFile_.flush();
        }
    }

    void EditorProjectPackager::LogError(ErrorCode code, const std::string& message)
    {
        LogLine(std::string("[Error] ") + ToString(code) + ": " + message);
    }

    std::string EditorProjectPackager::MakeTimestamp()
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

    std::string EditorProjectPackager::MakePackageDirectoryName(const std::string& projectName, const std::string& timestamp)
    {
        return SanitizePathSegment(projectName.empty() ? "VEngineProject" : projectName) + "_" + timestamp;
    }

    std::string EditorProjectPackager::SanitizePathSegment(std::string text)
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

    std::string EditorProjectPackager::PathToString(const Path& path)
    {
        return path.GetString();
    }
} // namespace ve::editor
