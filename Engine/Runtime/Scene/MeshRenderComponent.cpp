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
            transformChangedCallbackId_ =
                transform->AddTransformChangedCallback([this]() { MarkRenderItemTransformDirty(); });
        }
    }

    MeshRenderComponent::~MeshRenderComponent()
    {
        UnregisterTransformChangedCallback();
        UnregisterRenderItemFromRenderThread();
    }

    const std::string& MeshRenderComponent::GetMeshAssetPath() const noexcept
    {
        return meshAssetPath_;
    }

    void MeshRenderComponent::SetMeshAssetPath(std::string meshAssetPath)
    {
        meshAssetPath_ = std::move(meshAssetPath);
        SubmitRenderItemUpdateToRenderThread();
    }

    const std::string& MeshRenderComponent::GetMaterialAssetPath() const noexcept
    {
        return materialAssetPath_;
    }

    void MeshRenderComponent::SetMaterialAssetPath(std::string materialAssetPath)
    {
        materialAssetPath_ = std::move(materialAssetPath);
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

        return RTRenderItemDesc{
            meshAssetPath_,
            materialAssetPath_,
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
