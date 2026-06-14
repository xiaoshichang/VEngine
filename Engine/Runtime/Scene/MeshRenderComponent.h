#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Scene/Component.h"

#include <memory>
#include <string>

namespace ve
{
    /// Renderable static mesh attachment used by the first-stage forward renderer.
    class MeshRenderComponent final : public Component
    {
    public:
        MeshRenderComponent(Scene& scene, GameObject& owner);
        ~MeshRenderComponent() override;

        [[nodiscard]] const std::string& GetMeshAssetPath() const noexcept;
        void SetMeshAssetPath(std::string meshAssetPath);

        [[nodiscard]] const std::string& GetMaterialAssetPath() const noexcept;
        void SetMaterialAssetPath(std::string materialAssetPath);

        [[nodiscard]] const Vector3& GetBoundsCenter() const noexcept;
        void SetBoundsCenter(const Vector3& boundsCenter) noexcept;

        [[nodiscard]] const Vector3& GetBoundsExtents() const noexcept;
        void SetBoundsExtents(const Vector3& boundsExtents) noexcept;

        [[nodiscard]] bool IsVisible() const noexcept;
        void SetVisible(bool visible) noexcept;

        [[nodiscard]] std::shared_ptr<RTRenderItem> GetRTRenderItem() noexcept;
        [[nodiscard]] std::shared_ptr<const RTRenderItem> GetRTRenderItem() const noexcept;

    private:
        friend class GameObject;
        friend class Scene;

        [[nodiscard]] RTRenderItemDesc BuildRTDesc() const;
        void RegisterRTState();
        void UnregisterRTState() noexcept;
        void SubmitRTUpdate();

        std::string meshAssetPath_;
        std::string materialAssetPath_;
        Vector3 boundsCenter_ = Vector3::Zero();
        Vector3 boundsExtents_ = Vector3::One();
        bool visible_ = true;
        std::shared_ptr<RTRenderItem> rtRenderItem_;
    };
} // namespace ve
