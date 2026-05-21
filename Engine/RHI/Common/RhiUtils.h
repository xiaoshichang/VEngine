#pragma once

#include "Engine/RHI/Common/RhiTypes.h"

namespace ve::rhi
{
/// Returns true when the given format has a known backend representation.
[[nodiscard]] bool IsKnownFormat(RhiFormat format) noexcept;
}
