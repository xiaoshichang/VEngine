#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector3.h"

#include <memory>
#include <string>
#include <vector>

namespace ve
{
    /// Base render-thread resource placeholder used by RT scene objects.
    ///
    /// Concrete mesh, material, texture, and buffer resources will derive from or replace this shape once the Resource
    /// and upload systems exist. Scene-thread objects should hold RT objects through shared_ptr and update them through
    /// RenderCommand, not by touching RHI objects directly.
    class RHIResource : public NonCopyable
    {
    public:
        RHIResource() = default;
        virtual ~RHIResource() = default;
    };

    struct RTRenderItemDesc
    {
        std::string meshAssetPath;
        std::string materialAssetPath;
        Vector3 boundsCenter = Vector3::Zero();
        Vector3 boundsExtents = Vector3::One();
        bool visible = true;
    };

    /// Render-thread representation of one MeshRenderComponent.
    class RTRenderItem final : public NonCopyable
    {
    public:
        explicit RTRenderItem(RTRenderItemDesc desc);

        [[nodiscard]] const RTRenderItemDesc& GetDesc() const noexcept;
        void SetDesc(RTRenderItemDesc desc);

        [[nodiscard]] const std::shared_ptr<RHIResource>& GetMeshResource() const noexcept;
        void SetMeshResource(std::shared_ptr<RHIResource> resource) noexcept;

        [[nodiscard]] const std::shared_ptr<RHIResource>& GetMaterialResource() const noexcept;
        void SetMaterialResource(std::shared_ptr<RHIResource> resource) noexcept;

    private:
        RTRenderItemDesc desc_;
        std::shared_ptr<RHIResource> meshResource_;
        std::shared_ptr<RHIResource> materialResource_;
    };

    /// Render-thread scene owned by a Scene and later consumed by renderer/viewports/render textures.
    class RTScene final : public NonCopyable
    {
    public:
        RTScene() = default;
        ~RTScene() = default;

        void AddRenderItem(std::shared_ptr<RTRenderItem> item);
        void RemoveRenderItem(const std::shared_ptr<RTRenderItem>& item) noexcept;
        void Clear() noexcept;

        [[nodiscard]] SizeT GetRenderItemCount() const noexcept;
        [[nodiscard]] std::shared_ptr<RTRenderItem> GetRenderItem(SizeT index) const noexcept;

    private:
        std::vector<std::shared_ptr<RTRenderItem>> renderItems_;
    };
} // namespace ve
