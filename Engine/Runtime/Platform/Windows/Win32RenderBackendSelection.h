#pragma once

namespace ve
{
    enum class RenderBackend;

    /// Selects the Windows application render backend from the current process command line.
    [[nodiscard]] RenderBackend SelectWin32RenderBackendFromCommandLine();
} // namespace ve
