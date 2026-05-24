#pragma once

#include "Engine/Runtime/Core/Types.h"

namespace ve
{
    using ResourceId = UInt64;

    constexpr ResourceId InvalidResourceId = 0;

    template<typename T>
    class ResourceHandle
    {
    public:
        constexpr ResourceHandle() noexcept = default;
        explicit constexpr ResourceHandle(ResourceId id) noexcept
            : id_(id)
        {
        }

        [[nodiscard]] constexpr ResourceId GetId() const noexcept
        {
            return id_;
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return id_ != InvalidResourceId;
        }

        [[nodiscard]] friend constexpr bool operator==(ResourceHandle left, ResourceHandle right) noexcept
        {
            return left.id_ == right.id_;
        }

        [[nodiscard]] friend constexpr bool operator!=(ResourceHandle left, ResourceHandle right) noexcept
        {
            return !(left == right);
        }

    private:
        ResourceId id_ = InvalidResourceId;
    };
} // namespace ve
