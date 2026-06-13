#include "Engine/Runtime/Scene/MeshRenderComponent.h"

#include <utility>

namespace ve
{
    const std::string& MeshRenderComponent::GetMeshAssetPath() const noexcept
    {
        return meshAssetPath_;
    }

    void MeshRenderComponent::SetMeshAssetPath(std::string meshAssetPath)
    {
        meshAssetPath_ = std::move(meshAssetPath);
    }

    const std::string& MeshRenderComponent::GetMaterialAssetPath() const noexcept
    {
        return materialAssetPath_;
    }

    void MeshRenderComponent::SetMaterialAssetPath(std::string materialAssetPath)
    {
        materialAssetPath_ = std::move(materialAssetPath);
    }

    bool MeshRenderComponent::IsVisible() const noexcept
    {
        return visible_;
    }

    void MeshRenderComponent::SetVisible(bool visible) noexcept
    {
        visible_ = visible;
    }
} // namespace ve
