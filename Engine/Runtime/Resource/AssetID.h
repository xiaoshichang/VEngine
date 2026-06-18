#pragma once

#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/Types.h"

#include <cstddef>
#include <string>

namespace ve
{
    class AssetID
    {
    public:
        AssetID() = default;
        explicit AssetID(Guid guid, UInt64 subID = 0);

        [[nodiscard]] const Guid& GetGuid() const noexcept;
        [[nodiscard]] UInt64 GetSubID() const noexcept;
        [[nodiscard]] bool IsEmpty() const noexcept;
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] friend bool operator==(const AssetID& left, const AssetID& right) noexcept = default;

    private:
        Guid guid_;
        UInt64 subID_ = 0;
    };
} // namespace ve

namespace std
{
    template<>
    struct hash<ve::AssetID>
    {
        [[nodiscard]] size_t operator()(const ve::AssetID& id) const noexcept;
    };
} // namespace std
