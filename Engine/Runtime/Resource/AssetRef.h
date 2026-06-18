#pragma once

#include "Engine/Runtime/Resource/AssetID.h"
#include "Engine/Runtime/Resource/ResourceObject.h"

#include <utility>

namespace ve
{
    class AssetRefBase
    {
    public:
        [[nodiscard]] const AssetID& GetAssetID() const noexcept;
        [[nodiscard]] ResourceObject* GetResource() noexcept;
        [[nodiscard]] const ResourceObject* GetResource() const noexcept;
        [[nodiscard]] bool IsLoaded() const noexcept;

        void SetAssetID(AssetID id) noexcept;
        void SetResource(ResourceObject* resource) noexcept;
        void Reset() noexcept;

    private:
        AssetID assetID_;
        ResourceObject* resource_ = nullptr;
    };

    template<typename TResource>
    class AssetRef final : public AssetRefBase
    {
    public:
        [[nodiscard]] TResource* Get() noexcept
        {
            return static_cast<TResource*>(GetResource());
        }

        [[nodiscard]] const TResource* Get() const noexcept
        {
            return static_cast<const TResource*>(GetResource());
        }
    };
} // namespace ve
