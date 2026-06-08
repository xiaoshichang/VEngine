#pragma once

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
        ~Application();

        [[nodiscard]] int Run();

        [[nodiscard]] const std::string& GetName() const noexcept;
        [[nodiscard]] int GetExitCode() const noexcept;
        [[nodiscard]] EngineRuntime& GetRuntime() noexcept;
        [[nodiscard]] const EngineRuntime& GetRuntime() const noexcept;

    private:
        [[nodiscard]] ErrorCode InitializeEngineRuntime();
        [[nodiscard]] Result<std::unique_ptr<Window>> CreateMainWindow();
        [[nodiscard]] ErrorCode InitializeRendering(Window& mainWindow);
        void ShutdownRendering() noexcept;
        [[nodiscard]] int RunMainLoop(Window& mainWindow);
        [[nodiscard]] int RunApplication();

        ApplicationInitParam initParam_;
        EngineRuntime engineRuntime_;
        int exitCode_ = 0;
    };
} // namespace ve
