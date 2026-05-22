#pragma once

#include "Engine/Runtime/Platform/Window.h"

#include <string>

namespace ve
{
struct ApplicationDesc
{
    std::string name = "VEngine";
    WindowDesc mainWindow;
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

private:
    ApplicationDesc desc_;
    int exitCode_ = 0;
};
}
