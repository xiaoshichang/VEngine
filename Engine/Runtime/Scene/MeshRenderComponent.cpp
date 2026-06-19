#include "Engine/Runtime/Scene/MeshRenderComponent.h"

#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <utility>

namespace ve
{
    MeshRenderComponent::MeshRenderComponent(Scene& scene, GameObject& owner)
        : Component(scene, owner)
        , rtRenderItem_(std::make_shared<RTRenderItem>(BuildRenderItemDesc()))
    {
        TransformComponent* transform = owner.GetComponent<TransformComponent>();
        VE_ASSERT(transform != nullptr);
        if (transform != nullptr)
        {
            transformChangedCallbackId_ = transform->AddTransformChangedCallback([this]() { MarkRenderItemTransformDirty(); });
        }
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
        mesh_ = std::move(mesh);
        SubmitRenderItemUpdateToRenderThread();
    }

    const AssetID& MeshRenderComponent::GetMeshAssetID() const noexcept
    {
        return mesh_.GetAssetID();
    }

    void MeshRenderComponent::SetMeshAssetID(AssetID meshAssetID)
    {
        mesh_.SetAssetID(std::move(meshAssetID));
        SubmitRenderItemUpdateToRenderThread();
    }

    const AssetRef<MaterialResource>& MeshRenderComponent::GetMaterial() const noexcept
    {
        return material_;
    }

    void MeshRenderComponent::SetMaterial(AssetRef<MaterialResource> material)
    {
        material_ = std::move(material);
        SubmitRenderItemUpdateToRenderThread();
    }

    const AssetID& MeshRenderComponent::GetMaterialAssetID() const noexcept
    {
        return material_.GetAssetID();
    }

    void MeshRenderComponent::SetMaterialAssetID(AssetID materialAssetID)
    {
        material_.SetAssetID(std::move(materialAssetID));
        SubmitRenderItemUpdateToRenderThread();
    }

    const Vector3& MeshRenderComponent::GetBoundsCenter() const noexcept
    {
        return boundsCenter_;
    }

    void MeshRenderComponent::SetBoundsCenter(const Vector3& boundsCenter) noexcept
    {
        boundsCenter_ = boundsCenter;
        SubmitRenderItemUpdateToRenderThread();
    }

    const Vector3& MeshRenderComponent::GetBoundsExtents() const noexcept
    {
        return boundsExtents_;
    }

    void MeshRenderComponent::SetBoundsExtents(const Vector3& boundsExtents) noexcept
    {
        boundsExtents_ = boundsExtents;
        SubmitRenderItemUpdateToRenderThread();
    }

    std::shared_ptr<RTRenderItem> MeshRenderComponent::GetRTRenderItem() noexcept
    {
        return rtRenderItem_;
    }

    std::shared_ptr<const RTRenderItem> MeshRenderComponent::GetRTRenderItem() const noexcept
    {
        return rtRenderItem_;
    }

    RTRenderItemDesc MeshRenderComponent::BuildRenderItemDesc() const
    {
        const GameObject* owner = GetOwner();
        const TransformComponent* transform = owner != nullptr ? owner->GetComponent<TransformComponent>() : nullptr;
        const MeshResource* meshResource = mesh_.Get();
        const MaterialResource* materialResource = material_.Get();

        return RTRenderItemDesc{
            meshResource != nullptr ? meshResource->GetRTMeshResource() : nullptr,
            materialResource != nullptr ? materialResource->GetRTMaterialResource() : nullptr,
            boundsCenter_,
            boundsExtents_,
            transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity(),
        };
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
        scene->UpdateRenderItem(rtRenderItem_, BuildRenderItemDesc());
        renderThreadRegistered_ = true;
        ClearRenderItemTransformDirty();
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
        if (!IsEnabled() || !renderThreadRegistered_)
        {
            return;
        }

        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->UpdateRenderItem(rtRenderItem_, BuildRenderItemDesc());
        ClearRenderItemTransformDirty();
    }

    void MeshRenderComponent::SubmitRenderItemTransformUpdateToRenderThread()
    {
        SubmitRenderItemUpdateToRenderThread();
        ClearRenderItemTransformDirty();
    }

    bool MeshRenderComponent::IsRenderItemTransformDirty() const noexcept
    {
        return renderItemTransformDirty_;
    }

    void MeshRenderComponent::MarkRenderItemTransformDirty() noexcept
    {
        renderItemTransformDirty_ = true;
    }

    void MeshRenderComponent::ClearRenderItemTransformDirty() noexcept
    {
        renderItemTransformDirty_ = false;
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
