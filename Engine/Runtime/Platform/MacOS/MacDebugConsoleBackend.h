#pragma once

#include "Engine/Runtime/Platform/DebugConsoleBackend.h"

namespace ve
{
    class MacDebugConsoleBackend final : public DebugConsoleBackend
    {
    public:
        void Initialize() override;
        void SetCommandHandler(DebugConsoleCommandHandler handler) override;
        void PumpCommands() override;
        void WriteLog(LogSeverity severity, std::string_view line) override;
    };
} // namespace ve
