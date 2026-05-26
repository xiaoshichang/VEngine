#pragma once

#include <functional>
#include <string_view>

namespace ve
{
    enum class LogSeverity;

    using Win32DebugConsoleCommandHandler = std::function<void(std::string_view command)>;

    /// Initializes the engine-owned debug console. By default this is active only in debug builds; Windows Editor
    /// passes forceEnable so Release editor builds keep log output and GM command input available.
    void InitializeWin32DebugConsole(bool forceEnable = false);
    void SetWin32DebugConsoleCommandHandler(Win32DebugConsoleCommandHandler handler);
    void PumpWin32DebugConsoleCommands();
    void WriteWin32DebugConsoleLog(LogSeverity severity, std::string_view line);
} // namespace ve
