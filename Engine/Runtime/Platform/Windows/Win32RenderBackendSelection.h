#pragma once

namespace ve
{
    enum class RenderBackend;

    /// Selects D3D11 for --dx11 and D3D12 for --dx12 or the default Windows application path.
    [[nodiscard]] RenderBackend SelectWin32RenderBackendFromCommandLine();
} // namespace ve
