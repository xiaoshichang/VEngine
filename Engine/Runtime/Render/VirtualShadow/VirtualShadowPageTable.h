#pragma once

#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h"

#include <array>
#include <optional>
#include <span>

namespace ve
{
    class VirtualShadowPageTable
    {
    public:
        VirtualShadowPageTable() = default;

        void Clear() noexcept;
        [[nodiscard]] bool Insert(VirtualShadowPageKey key, UInt32 physicalPageIndex) noexcept;
        [[nodiscard]] std::optional<UInt32> Find(VirtualShadowPageKey key) const noexcept;
        [[nodiscard]] UInt32 GetSize() const noexcept;
        [[nodiscard]] std::span<const VirtualShadowGpuPageEntry> GetGpuEntries() const noexcept;

    private:
        std::array<VirtualShadowGpuPageEntry, VirtualShadowPageTableCapacity> entries_{};
        UInt32 size_ = 0;
    };
} // namespace ve
