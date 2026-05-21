#pragma once

#include <string>

namespace ve
{
class Application
{
public:
    explicit Application(std::string name);
    ~Application();

    void RunOnce();

    [[nodiscard]] const std::string& GetName() const noexcept;
    [[nodiscard]] int GetExitCode() const noexcept;

private:
    std::string name_;
    int exitCode_ = 0;
};
}
