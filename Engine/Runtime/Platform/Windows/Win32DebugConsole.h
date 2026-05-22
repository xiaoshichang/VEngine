#pragma once

#include <functional>
#include <string_view>

namespace ve
{
enum class LogSeverity;

using Win32DebugConsoleCommandHandler = std::function<void(std::string_view command)>;

void InitializeWin32DebugConsole();
void SetWin32DebugConsoleCommandHandler(Win32DebugConsoleCommandHandler handler);
void PumpWin32DebugConsoleCommands();
void WriteWin32DebugConsoleLog(LogSeverity severity, std::string_view line);
}
