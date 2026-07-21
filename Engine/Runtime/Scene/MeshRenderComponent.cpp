#include "Engine/Runtime/Scene/MeshRenderComponent.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <atomic>
#include <exception>
#include <limits>
#include <utility>

namespace ve
{
    namespace
    {
        std::atomic<UInt64> nextRenderItemID{1};

        [[nodiscard]] UInt64 AllocateRenderItemID() noexcept
        {
            const UInt64 renderItemID = nextRenderItemID.fetch_add(1, std::memory_order_relaxed);
            if (renderItemID == std::numeric_limits<UInt64>::max())
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "Mesh render item ID exhausted.");
                std::terminate();
            }
            return renderItemID;
        }

        void IncrementShadowRevision(UInt64& shadowRevision) noexcept
        {
            if (shadowRevision == std::numeric_limits<UInt64>::max())
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "Mesh shadow revision exhausted.");
                std::terminate();
            }
            ++shadowRevision;
        }
    } // namespace

    MeshRenderComponent::MeshRenderComponent(Scene& scene, GameObject& owner)
        : Component(scene, owner)
        , renderItemID_(AllocateRenderItemID())
        , rtRenderItem_(nullptr)
    {
        BuildRenderItem();
        RegisterTransformChangedCallback();
    }

    MeshRenderComponent::~MeshRenderComponent()
    {
        UnregisterTransformChangedCallback();
        UnregisterRenderItemFromRenderThread();
    }

    const AssetRef<MeshResource>& MeshRenderComponent::GetMesh() const noexcept
    {
        return mesh_;
    }

    void MeshRenderComponent::SetMesh(AssetRef<MeshResource> mesh)
    {
        if (mesh_.GetAssetID() == mesh.GetAssetID() && mesh_.Get() == mesh.Get())
        {
            return;
        }
        mesh_ = std::move(mesh);
        MarkRenderItemShadowDirty();
    }

    const AssetID& MeshRenderComponent::GetMeshAssetID() const noexcept
    {
        return mesh_.GetAssetID();
    }

    void MeshRenderComponent::SetMeshAssetID(AssetID meshAssetID)
    {
        if (mesh_.GetAssetID() == meshAssetID)
        {
            return;
        }
        mesh_.SetAssetID(std::move(meshAssetID));
        MarkRenderItemShadowDirty();
    }

    const AssetRef<MaterialResource>& MeshRenderComponent::GetMaterial() const noexcept
    {
        return material_;
    }

    void MeshRenderComponent::SetMaterial(AssetRef<MaterialResource> material)
    {
        material_ = std::move(material);
        MarkRenderItemDirty();
    }

    const AssetID& MeshRenderComponent::GetMaterialAssetID() const noexcept
    {
        return material_.GetAssetID();
    }

    void MeshRenderComponent::SetMaterialAssetID(AssetID materialAssetID)
    {
        material_.SetAssetID(std::move(materialAssetID));
        MarkRenderItemDirty();
    }

    const Vector3& MeshRenderComponent::GetBoundsCenter() const noexcept
    {
        return boundsCenter_;
    }

    void MeshRenderComponent::SetBoundsCenter(const Vector3& boundsCenter) noexcept
    {
        if (boundsCenter_ == boundsCenter)
        {
            return;
        }
        boundsCenter_ = boundsCenter;
        MarkRenderItemShadowDirty();
    }

    const Vector3& MeshRenderComponent::GetBoundsExtents() const noexcept
    {
        return boundsExtents_;
    }

    void MeshRenderComponent::SetBoundsExtents(const Vector3& boundsExtents) noexcept
    {
        if (boundsExtents_ == boundsExtents)
        {
            return;
        }
        boundsExtents_ = boundsExtents;
        MarkRenderItemShadowDirty();
    }

    UInt64 MeshRenderComponent::GetRenderItemID() const noexcept
    {
        return renderItemID_;
    }

    bool MeshRenderComponent::CastShadows() const noexcept
    {
        return castShadows_;
    }

    void MeshRenderComponent::SetCastShadows(bool castShadows) noexcept
    {
        if (castShadows_ == castShadows)
        {
            return;
        }
        castShadows_ = castShadows;
        MarkRenderItemShadowDirty();
    }

    bool MeshRenderComponent::ReceiveShadows() const noexcept
    {
        return receiveShadows_;
    }

    void MeshRenderComponent::SetReceiveShadows(bool receiveShadows) noexcept
    {
        if (receiveShadows_ == receiveShadows)
        {
            return;
        }
        receiveShadows_ = receiveShadows;
        MarkRenderItemShadowDirty();
    }

    UInt64 MeshRenderComponent::GetShadowRevision() const noexcept
    {
        return shadowRevision_;
    }

    std::shared_ptr<RTRenderItem> MeshRenderComponent::GetRTRenderItem() noexcept
    {
        return rtRenderItem_;
    }

    std::shared_ptr<const RTRenderItem> MeshRenderComponent::GetRTRenderItem() const noexcept
    {
        return rtRenderItem_;
    }

    RTRenderItemInitParam MeshRenderComponent::BuildRenderItemInitParam() const
    {
        const GameObject* owner = GetOwner();
        const TransformComponent* transform = owner != nullptr ? owner->GetComponent<TransformComponent>() : nullptr;
        const MeshResource* meshResource = mesh_.Get();
        const MaterialResource* materialResource = material_.Get();

        return RTRenderItemInitParam{
            meshResource != nullptr ? meshResource->GetRTMeshResource() : nullptr,
            materialResource != nullptr ? materialResource->GetRTMaterialResource() : nullptr,
            boundsCenter_,
            boundsExtents_,
            transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity(),
            renderItemID_,
            castShadows_,
            receiveShadows_,
            shadowRevision_,
        };
    }

    RTRenderItemUpdateParam MeshRenderComponent::BuildRenderItemUpdateParam() const
    {
        const GameObject* owner = GetOwner();
        const TransformComponent* transform = owner != nullptr ? owner->GetComponent<TransformComponent>() : nullptr;
        const MeshResource* meshResource = mesh_.Get();
        const MaterialResource* materialResource = material_.Get();

        return RTRenderItemUpdateParam{
            RTRenderItemDirtyFlags::All,
            meshResource != nullptr ? meshResource->GetRTMeshResource() : nullptr,
            materialResource != nullptr ? materialResource->GetRTMaterialResource() : nullptr,
            boundsCenter_,
            boundsExtents_,
            transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity(),
            renderItemID_,
            castShadows_,
            receiveShadows_,
            shadowRevision_,
        };
    }

    void MeshRenderComponent::BuildRenderItem()
    {
        auto initParam = BuildRenderItemInitParam();
        rtRenderItem_ = std::make_shared<RTRenderItem>(std::move(initParam));
    }

    void MeshRenderComponent::RegisterRenderItemToRenderThread()
    {
        if (!IsEnabled() || renderThreadRegistered_)
        {
            return;
        }

        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->RegisterRenderItem(rtRenderItem_);
        scene->UpdateRenderItem(rtRenderItem_, BuildRenderItemUpdateParam());
        renderThreadRegistered_ = true;
        ClearRenderItemDirty();
    }

    void MeshRenderComponent::UnregisterRenderItemFromRenderThread() noexcept
    {
        if (!renderThreadRegistered_)
        {
            return;
        }

        renderThreadRegistered_ = false;
        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->UnregisterRenderItem(rtRenderItem_);
    }

    void MeshRenderComponent::SubmitRenderItemUpdateToRenderThread()
    {
        if (!IsRenderItemDirty() || !IsEnabled() || !renderThreadRegistered_)
        {
            return;
        }

        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->UpdateRenderItem(rtRenderItem_, BuildRenderItemUpdateParam());
        ClearRenderItemDirty();
    }

    void MeshRenderComponent::SubmitRenderItemTransformUpdateToRenderThread()
    {
        SubmitRenderItemUpdateToRenderThread();
    }

    bool MeshRenderComponent::IsRenderItemDirty() const noexcept
    {
        return renderItemDirty_;
    }

    void MeshRenderComponent::MarkRenderItemDirty() noexcept
    {
        renderItemDirty_ = true;
    }

    void MeshRenderComponent::MarkRenderItemShadowDirty() noexcept
    {
        IncrementShadowRevision(shadowRevision_);
        MarkRenderItemDirty();
    }

    void MeshRenderComponent::ClearRenderItemDirty() noexcept
    {
        renderItemDirty_ = false;
    }

    void MeshRenderComponent::RegisterTransformChangedCallback() noexcept
    {
        TransformComponent* transform = owner_->GetComponent<TransformComponent>();
        VE_ASSERT(transform != nullptr);
        if (transform != nullptr)
        {
            transformChangedCallbackId_ = transform->AddTransformChangedCallback([this]() { MarkRenderItemShadowDirty(); });
        }
    }

    void MeshRenderComponent::UnregisterTransformChangedCallback() noexcept
    {
        if (transformChangedCallbackId_ == 0)
        {
            return;
        }

        if (GameObject* owner = GetOwner(); owner != nullptr)
        {
            TransformComponent* transform = owner->GetComponent<TransformComponent>();
            VE_ASSERT(transform != nullptr);
            transform->RemoveTransformChangedCallback(transformChangedCallbackId_);
        }

        transformChangedCallbackId_ = 0;
    }

    void MeshRenderComponent::SetEnabled(bool enabled) noexcept
    {
        if (IsEnabled() == enabled)
        {
            return;
        }

        Component::SetEnabled(enabled);
        if (enabled)
        {
            RegisterRenderItemToRenderThread();
        }
        else
        {
            UnregisterRenderItemFromRenderThread();
        }
    }

} // namespace ve
