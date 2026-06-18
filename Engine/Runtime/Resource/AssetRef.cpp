#include "Engine/Runtime/Resource/AssetRef.h"

#include "Engine/Runtime/Resource/ResourceObject.h"
#include "Engine/Runtime/Resource/ResourceSystem.h"

namespace ve
{
    AssetRefBase::~AssetRefBase()
    {
        Reset();
    }

    AssetRefBase::AssetRefBase(AssetRefBase&& other) noexcept
        : assetID_(std::move(other.assetID_))
        , resource_(std::exchange(other.resource_, nullptr))
        , resourceSystem_(std::exchange(other.resourceSystem_, nullptr))
    {
        other.assetID_ = AssetID();
    }

    AssetRefBase& AssetRefBase::operator=(AssetRefBase&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        Reset();
        assetID_ = std::move(other.assetID_);
        resource_ = std::exchange(other.resource_, nullptr);
        resourceSystem_ = std::exchange(other.resourceSystem_, nullptr);
        other.assetID_ = AssetID();
        return *this;
    }

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
        Reset();
        assetID_ = std::move(id);
    }

    void AssetRefBase::BindResource(ResourceSystem& resourceSystem, ResourceObject* resource) noexcept
    {
        Reset();
        resource_ = resource;
        resourceSystem_ = &resourceSystem;
        if (resource_ != nullptr)
        {
            assetID_ = resource_->GetAssetID();
        }
        else
        {
            resourceSystem_ = nullptr;
        }
    }

    void AssetRefBase::Reset() noexcept
    {
        if (resourceSystem_ != nullptr && !assetID_.IsEmpty())
        {
            (void)resourceSystem_->ReleaseResourceInternal(assetID_);
        }

        assetID_ = AssetID();
        resource_ = nullptr;
        resourceSystem_ = nullptr;
    }
} // namespace ve
