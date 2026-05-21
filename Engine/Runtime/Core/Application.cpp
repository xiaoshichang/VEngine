#include "Engine/Runtime/Core/Application.h"

#include <utility>

namespace ve
{
Application::Application(std::string name)
    : name_(std::move(name))
{
}

Application::~Application() = default;

void Application::RunOnce()
{
    exitCode_ = 0;
}

const std::string& Application::GetName() const noexcept
{
    return name_;
}

int Application::GetExitCode() const noexcept
{
    return exitCode_;
}
}
