#include "Engine/Runtime/Resource/AssetRef.h"

#include "Engine/Runtime/Resource/ResourceObject.h"

namespace ve
{
    const AssetID& AssetRefBase::GetAssetID() const noexcept
    {
        return assetID_;
    }

    ResourceObject* AssetRefBase::GetResource() noexcept
    {
        return resource_;
    }

    const ResourceObject* AssetRefBase::GetResource() const noexcept
    {
        return resource_;
    }

    bool AssetRefBase::IsLoaded() const noexcept
    {
        return resource_ != nullptr;
    }

    void AssetRefBase::SetAssetID(AssetID id) noexcept
    {
        assetID_ = std::move(id);
        resource_ = nullptr;
    }

    void AssetRefBase::SetResource(ResourceObject* resource) noexcept
    {
        resource_ = resource;
        if (resource_ != nullptr)
        {
            assetID_ = resource_->GetAssetID();
        }
    }

    void AssetRefBase::Reset() noexcept
    {
        assetID_ = AssetID();
        resource_ = nullptr;
    }
} // namespace ve
