#include "Engine/Runtime/Resource/AssetID.h"

#include <functional>
#include <utility>

namespace ve
{
    AssetID::AssetID(Guid guid, UInt64 subID)
        : guid_(std::move(guid))
        , subID_(subID)
    {
    }

    const Guid& AssetID::GetGuid() const noexcept
    {
        return guid_;
    }

    UInt64 AssetID::GetSubID() const noexcept
    {
        return subID_;
    }

    bool AssetID::IsEmpty() const noexcept
    {
        return guid_.IsEmpty();
    }

    std::string AssetID::ToString() const
    {
        return guid_.ToString() + ":" + std::to_string(subID_);
    }
} // namespace ve

namespace std
{
    size_t hash<ve::AssetID>::operator()(const ve::AssetID& id) const noexcept
    {
        const size_t guidHash = hash<ve::Guid>{}(id.GetGuid());
        const size_t subIDHash = hash<ve::UInt64>{}(id.GetSubID());
        return guidHash ^ (subIDHash + 0x9e3779b97f4a7c15ull + (guidHash << 6) + (guidHash >> 2));
    }
} // namespace std
