#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <memory>
#include <cstddef>
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

    struct RTShaderStageResourceDesc
    {
        rhi::RhiShaderStage stage = rhi::RhiShaderStage::Vertex;
        std::string entryPoint;
        std::vector<std::byte> d3d11Bytecode;
        std::vector<std::byte> d3d12Bytecode;
        std::string metalSource;
        std::string debugName;
    };

    struct RTShaderResourceDesc
    {
        std::string name = "ShaderResource";
        std::vector<RTShaderStageResourceDesc> stages;
    };

    class RTShaderResource final : public RHIResource
    {
    public:
        explicit RTShaderResource(RTShaderResourceDesc desc);

        [[nodiscard]] const RTShaderResourceDesc& GetDesc() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] rhi::RhiShaderModule* GetVertexShader() noexcept;
        [[nodiscard]] const rhi::RhiShaderModule* GetVertexShader() const noexcept;
        [[nodiscard]] rhi::RhiShaderModule* GetFragmentShader() noexcept;
        [[nodiscard]] const rhi::RhiShaderModule* GetFragmentShader() const noexcept;

        void InitRenderResource(rhi::RhiDevice& device, RTShaderResourceDesc desc);
        void ResetRenderResource() noexcept;

    private:
        RTShaderResourceDesc desc_;
        std::unique_ptr<rhi::RhiShaderModule> vertexShader_;
        std::unique_ptr<rhi::RhiShaderModule> fragmentShader_;
    };

    struct RTMaterialResourceDesc
    {
        std::string name = "MaterialResource";
        std::vector<std::byte> constantData;
        std::shared_ptr<RTShaderResource> shaderResource;
        UInt64 revision = 0;
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
        [[nodiscard]] std::shared_ptr<RTShaderResource> GetShaderResource() const noexcept;
        [[nodiscard]] UInt64 GetRevision() const noexcept;

        void InitRenderResource(rhi::RhiDevice& device, RTMaterialResourceDesc desc);
        void ResetRenderResource() noexcept;

    private:
        RTMaterialResourceDesc desc_;
        std::unique_ptr<rhi::RhiBuffer> uniformBuffer_;
    };
} // namespace ve
