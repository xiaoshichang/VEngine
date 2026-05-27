#pragma once

#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Platform/Window.h"
#include "Engine/Runtime/Scene/Scene.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ve
{
    using ApplicationSceneStartupFunction = std::function<ErrorCode(EngineRuntime&)>;
    using ApplicationSceneShutdownFunction = std::function<void(EngineRuntime&)>;
    using ApplicationWindowConfigureFunction = std::function<void(Window&, EngineRuntime&)>;
    using ApplicationFrameFunction = std::function<void(Window&, EngineRuntime&)>;
    using ApplicationCommandFunction = std::function<void(std::string_view, Window&, EngineRuntime&)>;

    struct ApplicationDesc
    {
        std::string name = "VEngine";
        WindowDesc mainWindow;
        EngineRuntimeDesc runtime;
        Path projectRoot;
        Path sampleScenePath;
        bool initializeRenderingOnStartup = true;
        ApplicationSceneStartupFunction sceneStartup;
        ApplicationSceneShutdownFunction sceneShutdown;
        ApplicationWindowConfigureFunction windowConfigure;
        ApplicationFrameFunction frameUpdate;
        ApplicationCommandFunction commandHandler;
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
        [[nodiscard]] ErrorCode CreateInitialScene();
        void DestroyInitialScene() noexcept;
        [[nodiscard]] ErrorCode CreateSampleScene();
        void DestroySampleScene() noexcept;
        [[nodiscard]] int RunMainLoop(Window& mainWindow);
        [[nodiscard]] int RunApplication();

        ApplicationDesc desc_;
        EngineRuntime engineRuntime_;
        std::unique_ptr<Scene> sampleScene_;
        int exitCode_ = 0;
    };
} // namespace ve
