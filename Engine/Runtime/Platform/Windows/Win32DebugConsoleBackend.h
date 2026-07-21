#pragma once

#include "Engine/Runtime/Platform/DebugConsoleBackend.h"

namespace ve
{
    class Win32DebugConsoleBackend final : public DebugConsoleBackend
    {
    public:
        void Initialize() override;
        void SetCommandHandler(DebugConsoleCommandHandler handler) override;
        void PumpCommands() override;
        void WriteLog(LogSeverity severity, std::string_view line) override;
        void PlaceNearWindow(void* nativeWindowHandle) override;
    };
} // namespace ve
