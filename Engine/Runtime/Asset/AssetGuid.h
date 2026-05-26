#pragma once

#include "Engine/Runtime/Core/Result.h"

#include <boost/uuid/uuid.hpp>
#include <string>
#include <string_view>

namespace ve
{
    class AssetGuid
    {
    public:
        AssetGuid() noexcept = default;

        [[nodiscard]] static AssetGuid Generate();
        [[nodiscard]] static Result<AssetGuid> Parse(std::string_view text);

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] friend bool operator==(const AssetGuid& left, const AssetGuid& right) noexcept = default;
        [[nodiscard]] friend bool operator!=(const AssetGuid& left, const AssetGuid& right) noexcept = default;

    private:
        explicit AssetGuid(const boost::uuids::uuid& uuid) noexcept;

        boost::uuids::uuid uuid_ = {};
    };
} // namespace ve
