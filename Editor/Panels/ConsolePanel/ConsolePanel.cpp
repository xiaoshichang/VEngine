#include "Editor/Panels/ConsolePanel/ConsolePanel.h"

#include <algorithm>
#include <deque>
#include <imgui.h>
#include <mutex>
#include <utility>

namespace ve::editor
{
    namespace
    {
        constexpr std::size_t MaxConsoleLogEntries = 4096;

        std::mutex gPendingLogsMutex;
        std::deque<EditorConsoleLogEntry> gPendingLogs;
        std::string gLastLogLine;

        [[nodiscard]] ImVec4 GetSeverityColor(LogSeverity severity) noexcept
        {
            switch (severity)
            {
            case LogSeverity::Trace:
                return ImVec4(0.55F, 0.55F, 0.55F, 1.0F);
            case LogSeverity::Debug:
                return ImVec4(0.55F, 0.75F, 1.0F, 1.0F);
            case LogSeverity::Info:
                return ImGui::GetStyleColorVec4(ImGuiCol_Text);
            case LogSeverity::Warn:
                return ImVec4(1.0F, 0.78F, 0.25F, 1.0F);
            case LogSeverity::Error:
                return ImVec4(1.0F, 0.35F, 0.30F, 1.0F);
            case LogSeverity::Fatal:
                return ImVec4(1.0F, 0.20F, 0.65F, 1.0F);
            }

            return ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }
    } // namespace

    void CaptureEditorLog(const LogRecord& record, std::string_view formattedLine) noexcept
    {
        try
        {
            EditorConsoleLogEntry entry;
            entry.severity = record.severity;
            entry.message.assign(formattedLine.data(), formattedLine.size());

            std::lock_guard lock(gPendingLogsMutex);
            if (gPendingLogs.size() >= MaxConsoleLogEntries)
            {
                gPendingLogs.pop_front();
            }
            gPendingLogs.push_back(std::move(entry));
            gLastLogLine.assign(formattedLine.data(), formattedLine.size());
        }
        catch (...)
        {
        }
    }

    std::string GetLastEditorLogLine()
    {
        std::lock_guard lock(gPendingLogsMutex);
        return gLastLogLine;
    }

    void ClearCapturedEditorLog() noexcept
    {
        std::lock_guard lock(gPendingLogsMutex);
        gPendingLogs.clear();
        gLastLogLine.clear();
    }

    const char* ConsolePanel::GetName() const noexcept
    {
        return "Console";
    }

    void ConsolePanel::RenderContent()
    {
        DrainPendingLogs();

        if (ImGui::Button("Clear"))
        {
            logEntries_.clear();
            ClearCapturedEditorLog();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoScroll_);
        ImGui::SameLine();
        ImGui::TextDisabled("%zu entries", logEntries_.size());
        ImGui::Separator();

        if (ImGui::BeginChild("ConsoleLog", ImVec2(0.0F, 0.0F), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
        {
            const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
            for (const EditorConsoleLogEntry& entry : logEntries_)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, GetSeverityColor(entry.severity));
                ImGui::TextUnformatted(entry.message.c_str());
                ImGui::PopStyleColor();
            }

            if (autoScroll_ && wasAtBottom)
            {
                ImGui::SetScrollHereY(1.0F);
            }
        }
        ImGui::EndChild();
    }

    void ConsolePanel::DrainPendingLogs()
    {
        std::deque<EditorConsoleLogEntry> pendingLogs;
        {
            std::lock_guard lock(gPendingLogsMutex);
            pendingLogs.swap(gPendingLogs);
        }

        if (pendingLogs.empty())
        {
            return;
        }

        const std::size_t availableEntryCount = MaxConsoleLogEntries - (std::min)(MaxConsoleLogEntries, pendingLogs.size());
        if (logEntries_.size() > availableEntryCount)
        {
            logEntries_.erase(logEntries_.begin(), logEntries_.begin() + static_cast<std::ptrdiff_t>(logEntries_.size() - availableEntryCount));
        }

        logEntries_.insert(logEntries_.end(), std::make_move_iterator(pendingLogs.begin()), std::make_move_iterator(pendingLogs.end()));
    }
} // namespace ve::editor
