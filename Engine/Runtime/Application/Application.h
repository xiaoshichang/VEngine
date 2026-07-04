#pragma once

#include "Engine/Runtime/Application/ApplicationCommandQueue.h"
#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Platform/Window.h"

#include <memory>
#include <string>

namespace ve
{
    struct ApplicationInitParam
    {
        std::string name = "VEngine";
        WindowDesc mainWindow;
        EngineRuntimeInitParam runtime;
    };

    class Application
    {
    public:
        explicit Application(std::string name);
        explicit Application(ApplicationInitParam desc);
        virtual ~Application();

        [[nodiscard]] virtual int Init();
        virtual void Run();
        virtual void UnInit();
        void StartMainLoop();
        [[nodiscard]] bool TickMainLoopFrame();

        [[nodiscard]] const std::string& GetName() const noexcept;
        [[nodiscard]] int GetExitCode() const noexcept;
        [[nodiscard]] void* GetMainWindowNativeHandle() const noexcept;
        [[nodiscard]] void* GetMainWindowNativeLayer() const noexcept;
        [[nodiscard]] EngineRuntime& GetRuntime() noexcept;
        [[nodiscard]] const EngineRuntime& GetRuntime() const noexcept;
        [[nodiscard]] ApplicationCommandQueue& GetMainThreadCommandQueue() noexcept;

    protected:
        [[nodiscard]] virtual ErrorCode InitializeRendering(Window& mainWindow);
        [[nodiscard]] virtual int RunMainLoop(Window& mainWindow);

    private:
        [[nodiscard]] ErrorCode InitializeEngineRuntime();
        [[nodiscard]] Result<std::unique_ptr<Window>> CreateMainWindow();

        ApplicationInitParam initParam_;
        EngineRuntime engineRuntime_;
        ApplicationCommandQueue mainThreadCommandQueue_;
        std::unique_ptr<Window> mainWindow_;
        bool mainLoopStarted_ = false;
        bool previousWindowVisible_ = false;
        bool previousWindowFocused_ = false;
        bool previousWindowMinimized_ = false;
        WindowExtent previousWindowExtent_ = {};
        bool initialized_ = false;
        int exitCode_ = 0;
    };
} // namespace ve
