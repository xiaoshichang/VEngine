#pragma once

#include "Editor/Core/EditorProject.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace ve::editor
{
    class Editor;

    enum class PackageRunStatus
    {
        Idle,
        Running,
        Succeeded,
        Failed,
    };

    enum class PackageStepStatus
    {
        Pending,
        Running,
        Succeeded,
        Failed,
    };

    enum class PackageTargetPlatform
    {
        Windows,
        Mac,
        IOS,
    };

    struct PackageTargetPlatformDesc
    {
        PackageTargetPlatform platform = PackageTargetPlatform::Windows;
        const char* displayName = "";
    };

    struct PackageStepState
    {
        std::string name;
        PackageStepStatus status = PackageStepStatus::Pending;
        std::string message;
    };

    struct PackageInfoDesc
    {
        Path packageInfoPath;
        std::string platform;
        std::string playerExecutable;
        std::string dataRoot;
        std::string appBundle;
    };

    class EditorProjectPacker : public NonMovable
    {
    public:
        EditorProjectPacker() = default;
        virtual ~EditorProjectPacker();

        void Reset();
        void Start(Editor& editor, const EditorProjectDescriptor::BuildSettings& buildSettings);
        void Advance(Editor& editor);

        [[nodiscard]] PackageRunStatus GetStatus() const noexcept;
        [[nodiscard]] float GetProgress() const noexcept;
        [[nodiscard]] const std::vector<PackageStepState>& GetSteps() const noexcept;
        [[nodiscard]] const std::string& GetStatusMessage() const noexcept;
        [[nodiscard]] const std::string& GetLogPath() const noexcept;
        [[nodiscard]] const std::string& GetOutputPath() const noexcept;

    protected:
        [[nodiscard]] virtual const char* GetPlatformName() const noexcept = 0;
        [[nodiscard]] virtual std::string GetRunningStatusMessage() const = 0;
        [[nodiscard]] virtual std::string GetSucceededStatusMessage() const = 0;
        virtual void ConfigurePackagePaths() = 0;
        virtual void InitializeSteps() = 0;
        [[nodiscard]] virtual ErrorCode RunStep(size_t stepIndex, Editor& editor) = 0;
        virtual void ResetPlatformState();

        [[nodiscard]] ErrorCode PreparePackageDirectories();
        [[nodiscard]] ErrorCode RefreshAssetDatabase(Editor& editor);
        [[nodiscard]] ErrorCode ExportAssetManifest(Editor& editor);
        [[nodiscard]] ErrorCode CopyRuntimeAssets(Editor& editor);
        [[nodiscard]] ErrorCode CopyManagedScripts();
        [[nodiscard]] ErrorCode WritePackageInfo(const PackageInfoDesc& desc);
        [[nodiscard]] ErrorCode CreateDirectory(const Path& path);
        [[nodiscard]] ErrorCode CopyFileWithDirectories(const Path& sourcePath, const Path& destinationPath);
        [[nodiscard]] ErrorCode CopyDirectory(const Path& sourcePath, const Path& destinationPath);

        void CompleteCurrentStep(std::string message);
        void FailCurrentStep(ErrorCode code, std::string message);
        void OpenLogFile();
        void CloseLogFile();
        void LogLine(const std::string& message);
        void LogError(ErrorCode code, const std::string& message);

        [[nodiscard]] static std::string MakeTimestamp();
        [[nodiscard]] static std::string MakePackageDirectoryName(const std::string& projectName, const std::string& timestamp);
        [[nodiscard]] static std::string SanitizePathSegment(std::string text);
        [[nodiscard]] static std::string PathToString(const Path& path);
        [[nodiscard]] static std::filesystem::path ToNativePath(const Path& path);

        PackageRunStatus status_ = PackageRunStatus::Idle;
        std::vector<PackageStepState> steps_;
        size_t currentStepIndex_ = 0;
        std::string statusMessage_;
        std::string timestamp_;
        std::string projectName_;
        EditorProjectDescriptor::BuildSettings buildSettings_;
        Path projectRoot_;
        Path buildRoot_;
        Path logDirectory_;
        Path logPath_;
        Path outputRoot_;
        Path packageBinRoot_;
        Path packageDataRoot_;
        Path packageRuntimeLogRoot_;
        std::string logPathText_;
        std::string outputPathText_;
        std::ofstream logFile_;
    };

    [[nodiscard]] std::vector<PackageTargetPlatformDesc> GetAvailableEditorPackageTargets();
    [[nodiscard]] std::unique_ptr<EditorProjectPacker> CreateEditorProjectPacker(PackageTargetPlatform platform);
    [[nodiscard]] std::unique_ptr<EditorProjectPacker> CreateEditorProjectPackerForHostPlatform();
} // namespace ve::editor
