#pragma once

#include "Engine/RHI/Common/RhiDevice.h"

#include <memory>

namespace ve::rhi
{
/// Creates a D3D11 RHI device that implements the common explicit RHI shape through a compatibility layer.
[[nodiscard]] std::unique_ptr<RhiDevice> CreateD3D11Device(bool enableDebug);
}
