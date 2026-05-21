#pragma once

#include "Engine/RHI/Common/RhiDevice.h"

#include <memory>

namespace ve::rhi
{
/// Creates a Metal RHI device that maps the common RHI directly to Metal command objects.
[[nodiscard]] std::unique_ptr<RhiDevice> CreateMetalDevice(bool enableDebug);
}
