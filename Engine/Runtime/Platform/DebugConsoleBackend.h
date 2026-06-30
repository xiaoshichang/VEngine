#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/Logging/Log.h"

#include <functional>
#include <memory>
#include <string_view>

namespace ve
{
    using DebugConsoleCommandHandler = std::function<void(std::string_view command)>;

    class DebugConsoleBackend : public NonMovable
    {
    public:
        virtual ~DebugConsoleBackend() = default;

        virtual void Initialize() = 0;
        virtual void SetCommandHandler(DebugConsoleCommandHandler handler) = 0;
        virtual void PumpCommands() = 0;
        virtual void WriteLog(LogSeverity severity, std::string_view line) = 0;
        virtual void PlaceNearWindow(void* nativeWindowHandle)
        {
            (void)nativeWindowHandle;
        }
    };

#if VE_PLATFORM_WINDOWS
    [[nodiscard]] std::unique_ptr<DebugConsoleBackend> CreateWin32DebugConsoleBackend();
#endif
#if VE_PLATFORM_MACOS
    [[nodiscard]] std::unique_ptr<DebugConsoleBackend> CreateMacDebugConsoleBackend();
#endif
} // namespace ve
