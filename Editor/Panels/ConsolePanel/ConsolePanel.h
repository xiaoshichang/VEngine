#pragma once

#include "Editor/Panels/BasePanel/BasePanel.h"
#include "Engine/Runtime/Logging/Log.h"

#include <string>
#include <vector>

namespace ve::editor
{
    struct EditorConsoleLogEntry
    {
        LogSeverity severity = LogSeverity::Info;
        std::string message;
    };

    void CaptureEditorLog(const LogRecord& record, std::string_view formattedLine) noexcept;
    [[nodiscard]] std::string GetLastEditorLogLine();
    void ClearCapturedEditorLog() noexcept;

    class ConsolePanel final : public BasePanel
    {
    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;
        void DrainPendingLogs();

        std::vector<EditorConsoleLogEntry> logEntries_;
        bool autoScroll_ = true;
    };
} // namespace ve::editor
