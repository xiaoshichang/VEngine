#pragma once

#include "Engine/RHI/Common/RhiDevice.h"

#include <memory>

namespace ve::rhi
{
/// Creates a D3D12 RHI device that maps the common RHI directly to explicit D3D12 objects.
[[nodiscard]] std::unique_ptr<RhiDevice> CreateD3D12Device(bool enableDebug);
}
