#pragma once

#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Platform/Window.h"
#include "Engine/Runtime/Scene/Scene.h"

#include <memory>
#include <string>

namespace ve
{
    struct ApplicationDesc
    {
        std::string name = "VEngine";
        WindowDesc mainWindow;
        EngineRuntimeDesc runtime;
    };

    class Application
    {
    public:
        explicit Application(std::string name);
        explicit Application(ApplicationDesc desc);
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
        void CreateSampleScene();
        void DestroySampleScene() noexcept;
        [[nodiscard]] int RunMainLoop(Window& mainWindow);
        [[nodiscard]] int RunApplication();

        ApplicationDesc desc_;
        EngineRuntime engineRuntime_;
        std::unique_ptr<Scene> sampleScene_;
        int exitCode_ = 0;
    };
} // namespace ve
