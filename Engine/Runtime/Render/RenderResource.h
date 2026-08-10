#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderResourceLifetime.h"
#include "Engine/Runtime/Render/RenderUniformBuffer.h"
#include "Engine/Runtime/Render/Renderer/RenderQueue.h"
#include "Engine/Runtime/Resource/ShaderPass.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ve
{
    /// Base type for render-thread resources referenced by RT scene objects.
    class RTResource : public NonCopyable
    {
    public:
        RTResource() = default;
        virtual ~RTResource() = default;
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
    class RTMeshResource final : public RTResource
    {
    public:
        explicit RTMeshResource(RTMeshResourceDesc desc);

        [[nodiscard]] const RTMeshResourceDesc& GetDesc() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetVertexBuffer() noexcept;
        [[nodiscard]] const rhi::RhiBuffer* GetVertexBuffer() const noexcept;
        [[nodiscard]] std::shared_ptr<rhi::RhiBuffer> GetVertexBufferShared() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetIndexBuffer() noexcept;
        [[nodiscard]] const rhi::RhiBuffer* GetIndexBuffer() const noexcept;
        [[nodiscard]] std::shared_ptr<rhi::RhiBuffer> GetIndexBufferShared() const noexcept;
        [[nodiscard]] UInt32 GetVertexStride() const noexcept;
        [[nodiscard]] UInt32 GetVertexCount() const noexcept;
        [[nodiscard]] UInt32 GetIndexCount() const noexcept;
        [[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;

        void InitRenderResource(rhi::RhiDevice& device, RTMeshResourceDesc desc);

    private:
        RTMeshResourceDesc desc_;
        std::shared_ptr<rhi::RhiBuffer> vertexBuffer_;
        std::shared_ptr<rhi::RhiBuffer> indexBuffer_;
    };

    struct RTTextureResourceDesc
    {
        std::string name = "TextureResource";
        UInt32 width = 0;
        UInt32 height = 0;
        UInt32 depth = 1;
        UInt32 mipLevelCount = 1;
        rhi::RhiFormat format = rhi::RhiFormat::Rgba8Unorm;
        rhi::RhiTextureUsage usage = rhi::RhiTextureUsage::Sampled;
        std::vector<std::byte> initialData;
        UInt32 initialDataRowPitch = 0;
    };

    /// Render Thread proxy for a texture resource.
    class RTTextureResource final : public RTResource
    {
    public:
        explicit RTTextureResource(RTTextureResourceDesc desc);

        [[nodiscard]] const RTTextureResourceDesc& GetDesc() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] rhi::RhiTexture* GetTexture() noexcept;
        [[nodiscard]] const rhi::RhiTexture* GetTexture() const noexcept;
        [[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;

        void InitRenderResource(rhi::RhiDevice& device, RTTextureResourceDesc desc);

    private:
        RTTextureResourceDesc desc_;
        std::shared_ptr<rhi::RhiTexture> texture_;
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
        [[nodiscard]] rhi::RhiShaderModule* GetComputeShader() noexcept { return computeShader_.get(); }
        [[nodiscard]] const rhi::RhiShaderModule* GetVertexShader() const noexcept { return vertexShader_.get(); }
        [[nodiscard]] const rhi::RhiShaderModule* GetFragmentShader() const noexcept { return fragmentShader_.get(); }
        [[nodiscard]] const rhi::RhiShaderModule* GetComputeShader() const noexcept { return computeShader_.get(); }
        [[nodiscard]] std::shared_ptr<rhi::RhiShaderModule> GetVertexShaderShared() const noexcept { return vertexShader_; }
        [[nodiscard]] std::shared_ptr<rhi::RhiShaderModule> GetFragmentShaderShared() const noexcept { return fragmentShader_; }
        [[nodiscard]] std::shared_ptr<rhi::RhiShaderModule> GetComputeShaderShared() const noexcept { return computeShader_; }
        std::shared_ptr<rhi::RhiShaderModule>& VertexShader() noexcept { return vertexShader_; }
        std::shared_ptr<rhi::RhiShaderModule>& FragmentShader() noexcept { return fragmentShader_; }
        std::shared_ptr<rhi::RhiShaderModule>& ComputeShader() noexcept { return computeShader_; }
        [[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;

    private:
        ShaderPassType type_;
        std::string name_;
        std::shared_ptr<rhi::RhiShaderModule> vertexShader_;
        std::shared_ptr<rhi::RhiShaderModule> fragmentShader_;
        std::shared_ptr<rhi::RhiShaderModule> computeShader_;
    };

    class RTShaderResource final : public RTResource
    {
    public:
        explicit RTShaderResource(RTShaderResourceDesc desc);

        [[nodiscard]] const RTShaderResourceDesc& GetDesc() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] RTShaderPass* GetPass(ShaderPassType type) noexcept;
        [[nodiscard]] const RTShaderPass* GetPass(ShaderPassType type) const noexcept;
        [[nodiscard]] RTShaderPass* GetPass(std::string_view name) noexcept;
        [[nodiscard]] const RTShaderPass* GetPass(std::string_view name) const noexcept;
        [[nodiscard]] bool HasPass(ShaderPassType type) const noexcept;
        [[nodiscard]] UInt64 GetRevision() const noexcept;
        [[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;

        void InitRenderResource(rhi::RhiDevice& device, RTShaderResourceDesc desc);

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
    class RTMaterialResource final : public RTResource
    {
    public:
        explicit RTMaterialResource(RTMaterialResourceDesc desc);

        [[nodiscard]] const RTMaterialResourceDesc& GetDesc() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] std::shared_ptr<RTShaderResource> GetShaderResource() const noexcept;
        [[nodiscard]] UInt64 GetRevision() const noexcept;
        [[nodiscard]] UniformBufferAllocation GetUniformBuffer(rhi::RhiDevice& device, UInt32 frameSlotIndex);
        [[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;

        void InitRenderResource(RTMaterialResourceDesc desc);

    private:
        RTMaterialResourceDesc desc_;
        RTDynamicUniformBuffer uniformBuffer_;
    };
} // namespace ve
