#include "Engine/Runtime/Platform/DebugConsole.h"

#include "Engine/Runtime/Core/Platform.h"

#include <iostream>
#include <mutex>
#include <utility>

namespace ve
{
    namespace
    {
        std::mutex gDebugConsoleMutex;
        std::unique_ptr<DebugConsoleBackend> gDebugConsoleBackend;

        [[nodiscard]] std::unique_ptr<DebugConsoleBackend> CreatePlatformDebugConsoleBackend()
        {
#if VE_PLATFORM_WINDOWS
            return CreateWin32DebugConsoleBackend();
#elif VE_PLATFORM_MACOS
            return CreateMacDebugConsoleBackend();
#else
            return nullptr;
#endif
        }

        void WriteFallbackConsoleLog(LogSeverity severity, std::string_view line)
        {
            std::ostream& stream = severity >= LogSeverity::Warn ? std::cerr : std::clog;
            stream << line << '\n';
            stream.flush();
        }

        DebugConsoleBackend* GetOrCreateDebugConsoleBackend()
        {
            std::lock_guard lock(gDebugConsoleMutex);
            if (gDebugConsoleBackend == nullptr)
            {
                gDebugConsoleBackend = CreatePlatformDebugConsoleBackend();
            }

            return gDebugConsoleBackend.get();
        }
    } // namespace

    void InitializeDebugConsole()
    {
        DebugConsoleBackend* backend = GetOrCreateDebugConsoleBackend();
        if (backend != nullptr)
        {
            backend->Initialize();
        }
    }

    void SetDebugConsoleCommandHandler(DebugConsoleCommandHandler handler)
    {
        DebugConsoleBackend* backend = GetOrCreateDebugConsoleBackend();
        if (backend != nullptr)
        {
            backend->SetCommandHandler(std::move(handler));
        }
    }

    void PumpDebugConsoleCommands()
    {
        DebugConsoleBackend* backend = GetOrCreateDebugConsoleBackend();
        if (backend != nullptr)
        {
            backend->PumpCommands();
        }
    }

    void WriteDebugConsoleLog(LogSeverity severity, std::string_view line)
    {
        DebugConsoleBackend* backend = GetOrCreateDebugConsoleBackend();
        if (backend != nullptr)
        {
            backend->WriteLog(severity, line);
            return;
        }

        WriteFallbackConsoleLog(severity, line);
    }

    void PlaceDebugConsoleNearWindow(void* nativeWindowHandle)
    {
        DebugConsoleBackend* backend = GetOrCreateDebugConsoleBackend();
        if (backend != nullptr)
        {
            backend->PlaceNearWindow(nativeWindowHandle);
        }
    }
} // namespace ve
