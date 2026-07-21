#include "Engine/RHI/Common/RhiUtils.h"

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
} // namespace ve::rhi
