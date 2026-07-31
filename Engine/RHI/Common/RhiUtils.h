#pragma once

#include "Engine/RHI/Common/RhiTypes.h"

namespace ve::rhi
{
    struct RhiStructuredBufferRange
    {
        uint32_t firstElement = 0;
        uint32_t elementCount = 0;
    };

    /// Returns true when the given format has a known backend representation.
    [[nodiscard]] bool IsKnownFormat(RhiFormat format) noexcept;

    /// Returns true when a pipeline resource layout has a valid backing array and no duplicate kind/stage/slot entries.
    [[nodiscard]] bool IsPipelineResourceLayoutValid(const RhiPipelineResourceLayoutDesc& layout) noexcept;

    /// Converts an aligned structured-buffer byte range to the element range required by native views.
    [[nodiscard]] bool
    TryBuildStructuredBufferRange(uint64_t bufferSize, uint32_t structureStride, uint64_t offset, uint64_t size, RhiStructuredBufferRange& range) noexcept;
} // namespace ve::rhi
