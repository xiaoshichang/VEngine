#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector4.h"

#include <memory>
#include <string>
#include <vector>

namespace ve
{
    /// Base type for render-thread resources referenced by RT scene objects.
    class RHIResource : public NonCopyable
    {
    public:
        RHIResource() = default;
        virtual ~RHIResource() = default;
    };

    struct RTMeshVertex
    {
        Float32 position[3] = {};
        Float32 normal[3] = {0.0f, 1.0f, 0.0f};
    };

    struct RTMeshResourceDesc
    {
        std::string name = "MeshResource";
        std::vector<RTMeshVertex> vertices;
        std::vector<UInt32> indices;
    };

    /// Render Thread proxy for a static mesh resource.
    ///
    /// The proxy owns backend buffers and can be safely captured by render commands after the Scene Thread releases the
    /// CPU-side MeshResource.
    class RTMeshResource final : public RHIResource
    {
    public:
        explicit RTMeshResource(RTMeshResourceDesc desc);

        [[nodiscard]] const RTMeshResourceDesc& GetDesc() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetVertexBuffer() noexcept;
        [[nodiscard]] const rhi::RhiBuffer* GetVertexBuffer() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetIndexBuffer() noexcept;
        [[nodiscard]] const rhi::RhiBuffer* GetIndexBuffer() const noexcept;
        [[nodiscard]] UInt32 GetVertexStride() const noexcept;
        [[nodiscard]] UInt32 GetVertexCount() const noexcept;
        [[nodiscard]] UInt32 GetIndexCount() const noexcept;

        void InitRenderResource(rhi::RhiDevice& device, RTMeshResourceDesc desc);
        void ResetRenderResource() noexcept;

    private:
        RTMeshResourceDesc desc_;
        std::unique_ptr<rhi::RhiBuffer> vertexBuffer_;
        std::unique_ptr<rhi::RhiBuffer> indexBuffer_;
    };

    struct RTMaterialResourceDesc
    {
        std::string name = "MaterialResource";
        Vector4 baseColor = Vector4::One();
    };

    /// Packed constants uploaded for the first-stage material resource.
    struct RTMaterialUniformData
    {
        Float32 baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        Float32 padding[60] = {};
    };

    /// Render Thread proxy for a material resource.
    class RTMaterialResource final : public RHIResource
    {
    public:
        explicit RTMaterialResource(RTMaterialResourceDesc desc);

        [[nodiscard]] const RTMaterialResourceDesc& GetDesc() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetUniformBuffer() noexcept;
        [[nodiscard]] const rhi::RhiBuffer* GetUniformBuffer() const noexcept;

        void InitRenderResource(rhi::RhiDevice& device, RTMaterialResourceDesc desc);
        void ResetRenderResource() noexcept;

    private:
        RTMaterialResourceDesc desc_;
        std::unique_ptr<rhi::RhiBuffer> uniformBuffer_;
    };
} // namespace ve
