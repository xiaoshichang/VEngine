#include "Engine/RHI/Common/RhiUtils.h"

#include <limits>

namespace ve::rhi
{
    bool IsKnownFormat(RhiFormat format) noexcept
    {
        return format != RhiFormat::Unknown;
    }

    bool IsPipelineResourceLayoutValid(const RhiPipelineResourceLayoutDesc& layout) noexcept
    {
        if (layout.bindingCount != 0 && layout.bindings == nullptr)
        {
            return false;
        }

        for (uint32_t leftIndex = 0; leftIndex < layout.bindingCount; ++leftIndex)
        {
            const RhiPipelineResourceBindingDesc& left = layout.bindings[leftIndex];
            for (uint32_t rightIndex = leftIndex + 1; rightIndex < layout.bindingCount; ++rightIndex)
            {
                const RhiPipelineResourceBindingDesc& right = layout.bindings[rightIndex];
                if (left.kind == right.kind && left.stage == right.stage && left.slot == right.slot)
                {
                    return false;
                }
            }
        }

        return true;
    }

    bool TryBuildStructuredBufferRange(uint64_t bufferSize, uint32_t structureStride, uint64_t offset, uint64_t size, RhiStructuredBufferRange& range) noexcept
    {
        range = {};
        if (structureStride == 0 || size == 0 || offset > bufferSize || size > bufferSize - offset || offset % structureStride != 0 ||
            size % structureStride != 0)
        {
            return false;
        }

        const uint64_t firstElement = offset / structureStride;
        const uint64_t elementCount = size / structureStride;
        if (firstElement > std::numeric_limits<uint32_t>::max() || elementCount > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }

        range.firstElement = static_cast<uint32_t>(firstElement);
        range.elementCount = static_cast<uint32_t>(elementCount);
        return true;
    }
} // namespace ve::rhi
