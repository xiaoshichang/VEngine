#pragma once

#include "Engine/RHI/Common/RhiDevice.h"

#include <memory>

namespace ve::rhi
{
    /// Creates a D3D11 RHI device that implements the common explicit RHI shape through a compatibility layer.
    [[nodiscard]] std::unique_ptr<RhiDevice> CreateD3D11Device(bool enableDebug);
    /// Returns the number of indexed-instanced draw calls recorded since the command list's last Begin.
    [[nodiscard]] uint64_t GetD3D11RecordedDrawIndexedInstancedCount(const RhiCommandList& commandList) noexcept;
} // namespace ve::rhi
