#pragma once

#include "Engine/RHI/Common/RhiDevice.h"

#include <memory>

namespace ve::rhi
{
    /// Creates a D3D12 RHI device that maps the common RHI directly to explicit D3D12 objects.
    [[nodiscard]] std::unique_ptr<RhiDevice> CreateD3D12Device(bool enableDebug);
    /// Returns the number of indexed-instanced draw calls recorded since the command list's last Begin.
    [[nodiscard]] uint64_t GetD3D12RecordedDrawIndexedInstancedCount(const RhiCommandList& commandList) noexcept;
} // namespace ve::rhi
