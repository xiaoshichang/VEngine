#pragma once

#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Platform/Window.h"

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
    ApplicationDesc desc_;
    EngineRuntime runtime_;
    int exitCode_ = 0;
};
}
