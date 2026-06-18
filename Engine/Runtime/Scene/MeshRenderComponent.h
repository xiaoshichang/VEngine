#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Resource/AssetRef.h"
#include "Engine/Runtime/Resource/ResourceObject.h"
#include "Engine/Runtime/Scene/Component.h"

#include <memory>

namespace ve
{
    /// Renderable static mesh attachment used by the first-stage forward renderer.
    class MeshRenderComponent final : public Component
    {
    public:
        MeshRenderComponent(Scene& scene, GameObject& owner);
        ~MeshRenderComponent() override;

        [[nodiscard]] const AssetRef<MeshResource>& GetMesh() const noexcept;
        void SetMesh(AssetRef<MeshResource> mesh);
        [[nodiscard]] const AssetID& GetMeshAssetID() const noexcept;
        void SetMeshAssetID(AssetID meshAssetID);

        [[nodiscard]] const AssetRef<MaterialResource>& GetMaterial() const noexcept;
        void SetMaterial(AssetRef<MaterialResource> material);
        [[nodiscard]] const AssetID& GetMaterialAssetID() const noexcept;
        void SetMaterialAssetID(AssetID materialAssetID);

        [[nodiscard]] const Vector3& GetBoundsCenter() const noexcept;
        void SetBoundsCenter(const Vector3& boundsCenter) noexcept;

        [[nodiscard]] const Vector3& GetBoundsExtents() const noexcept;
        void SetBoundsExtents(const Vector3& boundsExtents) noexcept;

        [[nodiscard]] std::shared_ptr<RTRenderItem> GetRTRenderItem() noexcept;
        [[nodiscard]] std::shared_ptr<const RTRenderItem> GetRTRenderItem() const noexcept;

        void SetEnabled(bool enabled) noexcept override;

    private:
        friend class GameObject;
        friend class Scene;

        [[nodiscard]] RTRenderItemDesc BuildRenderItemDesc() const;
        [[nodiscard]] bool IsRenderItemTransformDirty() const noexcept;
        void MarkRenderItemTransformDirty() noexcept;
        void ClearRenderItemTransformDirty() noexcept;
        void UnregisterTransformChangedCallback() noexcept;
        void RegisterRenderItemToRenderThread();
        void UnregisterRenderItemFromRenderThread() noexcept;
        void SubmitRenderItemUpdateToRenderThread();
        void SubmitRenderItemTransformUpdateToRenderThread();

        AssetRef<MeshResource> mesh_;
        AssetRef<MaterialResource> material_;
        Vector3 boundsCenter_ = Vector3::Zero();
        Vector3 boundsExtents_ = Vector3::One();
        bool renderItemTransformDirty_ = true;
        bool renderThreadRegistered_ = false;
        UInt64 transformChangedCallbackId_ = 0;
        std::shared_ptr<RTRenderItem> rtRenderItem_;
    };
} // namespace ve
