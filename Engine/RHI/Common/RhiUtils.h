#pragma once

#include "Engine/RHI/Common/RhiTypes.h"

namespace ve::rhi
{
    /// Returns true when the given format has a known backend representation.
    [[nodiscard]] bool IsKnownFormat(RhiFormat format) noexcept;

    /// Returns true when a pipeline resource layout has a valid backing array and no duplicate kind/stage/slot entries.
    [[nodiscard]] bool IsPipelineResourceLayoutValid(const RhiPipelineResourceLayoutDesc& layout) noexcept;
} // namespace ve::rhi
