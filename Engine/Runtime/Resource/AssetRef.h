#pragma once

#include "Engine/Runtime/Resource/AssetID.h"
#include "Engine/Runtime/Resource/ResourceObject.h"

#include <utility>

namespace ve
{
    class ResourceSystem;

    class AssetRefBase
    {
    public:
        AssetRefBase() = default;
        ~AssetRefBase();

        AssetRefBase(const AssetRefBase&) = delete;
        AssetRefBase& operator=(const AssetRefBase&) = delete;
        AssetRefBase(AssetRefBase&& other) noexcept;
        AssetRefBase& operator=(AssetRefBase&& other) noexcept;

        [[nodiscard]] const AssetID& GetAssetID() const noexcept;
        [[nodiscard]] ResourceObject* GetResource() noexcept;
        [[nodiscard]] const ResourceObject* GetResource() const noexcept;
        [[nodiscard]] bool IsLoaded() const noexcept;

        void SetAssetID(AssetID id) noexcept;
        void Reset() noexcept;

    private:
        friend class ResourceSystem;

        void BindResource(ResourceSystem& resourceSystem, ResourceObject* resource) noexcept;

        AssetID assetID_;
        ResourceObject* resource_ = nullptr;
        ResourceSystem* resourceSystem_ = nullptr;
    };

    template<typename TResource>
    class AssetRef final : public AssetRefBase
    {
    public:
        AssetRef() = default;
        ~AssetRef() = default;

        AssetRef(const AssetRef&) = delete;
        AssetRef& operator=(const AssetRef&) = delete;
        AssetRef(AssetRef&& other) noexcept = default;
        AssetRef& operator=(AssetRef&& other) noexcept = default;

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
