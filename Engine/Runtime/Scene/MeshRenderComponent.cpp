#include "Engine/Runtime/Scene/MeshRenderComponent.h"

#include "Engine/Runtime/Scene/Scene.h"

#include <utility>

namespace ve
{
    MeshRenderComponent::MeshRenderComponent(Scene& scene, GameObject& owner)
        : Component(scene, owner)
        , rtRenderItem_(std::make_shared<RTRenderItem>(BuildRTDesc()))
    {
    }

    MeshRenderComponent::~MeshRenderComponent()
    {
        UnregisterRTState();
    }

    const std::string& MeshRenderComponent::GetMeshAssetPath() const noexcept
    {
        return meshAssetPath_;
    }

    void MeshRenderComponent::SetMeshAssetPath(std::string meshAssetPath)
    {
        meshAssetPath_ = std::move(meshAssetPath);
        SubmitRTUpdate();
    }

    const std::string& MeshRenderComponent::GetMaterialAssetPath() const noexcept
    {
        return materialAssetPath_;
    }

    void MeshRenderComponent::SetMaterialAssetPath(std::string materialAssetPath)
    {
        materialAssetPath_ = std::move(materialAssetPath);
        SubmitRTUpdate();
    }

    const Vector3& MeshRenderComponent::GetBoundsCenter() const noexcept
    {
        return boundsCenter_;
    }

    void MeshRenderComponent::SetBoundsCenter(const Vector3& boundsCenter) noexcept
    {
        boundsCenter_ = boundsCenter;
        SubmitRTUpdate();
    }

    const Vector3& MeshRenderComponent::GetBoundsExtents() const noexcept
    {
        return boundsExtents_;
    }

    void MeshRenderComponent::SetBoundsExtents(const Vector3& boundsExtents) noexcept
    {
        boundsExtents_ = boundsExtents;
        SubmitRTUpdate();
    }

    bool MeshRenderComponent::IsVisible() const noexcept
    {
        return visible_;
    }

    void MeshRenderComponent::SetVisible(bool visible) noexcept
    {
        visible_ = visible;
        SubmitRTUpdate();
    }

    std::shared_ptr<RTRenderItem> MeshRenderComponent::GetRTRenderItem() noexcept
    {
        return rtRenderItem_;
    }

    std::shared_ptr<const RTRenderItem> MeshRenderComponent::GetRTRenderItem() const noexcept
    {
        return rtRenderItem_;
    }

    RTRenderItemDesc MeshRenderComponent::BuildRTDesc() const
    {
        return RTRenderItemDesc{
            meshAssetPath_,
            materialAssetPath_,
            boundsCenter_,
            boundsExtents_,
            visible_,
        };
    }

    void MeshRenderComponent::RegisterRTState()
    {
        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->RegisterRenderItem(rtRenderItem_);
        scene->UpdateRenderItem(rtRenderItem_, BuildRTDesc());
    }

    void MeshRenderComponent::UnregisterRTState() noexcept
    {
        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->UnregisterRenderItem(rtRenderItem_);
    }

    void MeshRenderComponent::SubmitRTUpdate()
    {
        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->UpdateRenderItem(rtRenderItem_, BuildRTDesc());
    }

} // namespace ve
