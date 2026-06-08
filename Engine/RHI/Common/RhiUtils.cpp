#include "Engine/RHI/Common/RhiUtils.h"

namespace ve::rhi
{
    bool IsKnownFormat(RhiFormat format) noexcept
    {
        return format != RhiFormat::Unknown;
    }
} // namespace ve::rhi
