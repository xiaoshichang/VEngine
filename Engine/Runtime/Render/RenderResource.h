#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/MaterialUniformPool.h"
#include "Engine/Runtime/Render/Renderer/RenderQueue.h"
#include "Engine/Runtime/Resource/ShaderPass.h"

#include <cstddef>
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

        void InitRenderResource(rhi::RhiDevice& device,
                                RTMeshResourceDesc desc,
                                std::vector<std::unique_ptr<rhi::RhiObject>>& retiredResources);
        void ResetRenderResource(std::vector<std::unique_ptr<rhi::RhiObject>>& retiredResources) noexcept;

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

    struct RTShaderPassResourceDesc
    {
        ShaderPassType type = ShaderPassType::OpaqueForward;
        std::string name;
        std::vector<RTShaderStageResourceDesc> stages;
    };

    struct RTShaderResourceDesc
    {
        std::string name = "ShaderResource";
        std::vector<RTShaderPassResourceDesc> passes;
    };

    class RTShaderPass final : public NonCopyable
    {
    public:
        RTShaderPass(ShaderPassType type, std::string name) : type_(type), name_(std::move(name)) {}
        [[nodiscard]] ShaderPassType GetType() const noexcept { return type_; }
        [[nodiscard]] const std::string& GetName() const noexcept { return name_; }
        [[nodiscard]] rhi::RhiShaderModule* GetVertexShader() noexcept { return vertexShader_.get(); }
        [[nodiscard]] rhi::RhiShaderModule* GetFragmentShader() noexcept { return fragmentShader_.get(); }
        [[nodiscard]] const rhi::RhiShaderModule* GetVertexShader() const noexcept { return vertexShader_.get(); }
        [[nodiscard]] const rhi::RhiShaderModule* GetFragmentShader() const noexcept { return fragmentShader_.get(); }
        std::unique_ptr<rhi::RhiShaderModule>& VertexShader() noexcept { return vertexShader_; }
        std::unique_ptr<rhi::RhiShaderModule>& FragmentShader() noexcept { return fragmentShader_; }

    private:
        ShaderPassType type_;
        std::string name_;
        std::unique_ptr<rhi::RhiShaderModule> vertexShader_;
        std::unique_ptr<rhi::RhiShaderModule> fragmentShader_;
    };

    class RTShaderResource final : public RHIResource
    {
    public:
        explicit RTShaderResource(RTShaderResourceDesc desc);

        [[nodiscard]] const RTShaderResourceDesc& GetDesc() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] RTShaderPass* GetPass(ShaderPassType type) noexcept;
        [[nodiscard]] const RTShaderPass* GetPass(ShaderPassType type) const noexcept;
        [[nodiscard]] bool HasPass(ShaderPassType type) const noexcept;
        [[nodiscard]] UInt64 GetRevision() const noexcept;

        void InitRenderResource(rhi::RhiDevice& device,
                                RTShaderResourceDesc desc,
                                std::vector<std::unique_ptr<rhi::RhiObject>>& retiredResources);
        void ResetRenderResource(std::vector<std::unique_ptr<rhi::RhiObject>>& retiredResources) noexcept;

    private:
        RTShaderResourceDesc desc_;
        std::vector<std::unique_ptr<RTShaderPass>> passes_;
        UInt64 revision_ = 0;
    };

    struct RTMaterialResourceDesc
    {
        std::string name = "MaterialResource";
        std::vector<std::byte> constantData;
        std::shared_ptr<RTShaderResource> shaderResource;
        RenderQueue renderQueue = RenderQueue::Opaque;
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
        [[nodiscard]] UInt64 GetUniformBufferOffset() const noexcept;
        [[nodiscard]] UInt64 GetUniformBufferSize() const noexcept;
        [[nodiscard]] std::shared_ptr<RTShaderResource> GetShaderResource() const noexcept;
        [[nodiscard]] UInt64 GetRevision() const noexcept;

        void InitRenderResource(MaterialUniformPool& uniformPool, RTMaterialResourceDesc desc);
        void ResetRenderResource(MaterialUniformPool& uniformPool);

    private:
        RTMaterialResourceDesc desc_;
        MaterialUniformAllocation uniformAllocation_;
    };
} // namespace ve
