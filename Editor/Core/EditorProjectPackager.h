#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <fstream>
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

    struct PackageStepState
    {
        std::string name;
        PackageStepStatus status = PackageStepStatus::Pending;
        std::string message;
    };

    class EditorProjectPackager : public NonMovable
    {
    public:
        EditorProjectPackager() = default;
        ~EditorProjectPackager();

        void Reset();
        void StartWindowsPackage(Editor& editor);
        void Advance(Editor& editor);

        [[nodiscard]] PackageRunStatus GetStatus() const noexcept;
        [[nodiscard]] float GetProgress() const noexcept;
        [[nodiscard]] const std::vector<PackageStepState>& GetSteps() const noexcept;
        [[nodiscard]] const std::string& GetStatusMessage() const noexcept;
        [[nodiscard]] const std::string& GetLogPath() const noexcept;
        [[nodiscard]] const std::string& GetOutputPath() const noexcept;

    private:
        [[nodiscard]] ErrorCode PrepareDirectories();
        [[nodiscard]] ErrorCode RefreshAssetDatabase(Editor& editor);
        [[nodiscard]] ErrorCode ExportAssetManifest(Editor& editor);
        [[nodiscard]] ErrorCode CopyRuntimeAssets(Editor& editor);
        [[nodiscard]] ErrorCode CopyManagedScripts();
        [[nodiscard]] ErrorCode CopyWindowsPlayerExecutable();
        [[nodiscard]] ErrorCode CopyWindowsPlayerManagedRuntime();
        [[nodiscard]] ErrorCode WritePackageInfo();

        void InitializeSteps();
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

        PackageRunStatus status_ = PackageRunStatus::Idle;
        std::vector<PackageStepState> steps_;
        size_t currentStepIndex_ = 0;
        std::string statusMessage_;
        std::string timestamp_;
        std::string projectName_;
        Path projectRoot_;
        Path buildRoot_;
        Path logDirectory_;
        Path logPath_;
        Path outputRoot_;
        Path packageBinRoot_;
        Path packageDataRoot_;
        std::string logPathText_;
        std::string outputPathText_;
        std::ofstream logFile_;
    };
} // namespace ve::editor
