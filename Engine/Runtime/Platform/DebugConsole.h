#pragma once

#include "Engine/Runtime/Platform/DebugConsoleBackend.h"

namespace ve
{
    void InitializeDebugConsole();
    void SetDebugConsoleCommandHandler(DebugConsoleCommandHandler handler);
    void PumpDebugConsoleCommands();
    void WriteDebugConsoleLog(LogSeverity severity, std::string_view line);
    void PlaceDebugConsoleNearWindow(void* nativeWindowHandle);
} // namespace ve
