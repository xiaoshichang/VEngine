#pragma once

namespace ve
{
    class Win32MessageLoop final
    {
    public:
        enum class PumpResult
        {
            Continue,
            Quit,
        };

        [[nodiscard]] PumpResult PumpPendingMessages();
        [[nodiscard]] int GetQuitExitCode() const noexcept;

    private:
        int quitExitCode_ = 0;
    };
} // namespace ve
