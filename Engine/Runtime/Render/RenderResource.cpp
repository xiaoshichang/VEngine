#include "Engine/Runtime/Render/RenderResource.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] rhi::RhiBufferDesc MakeBufferDesc(UInt64 size,
                                                        rhi::RhiBufferUsage usage,
                                                        const void* initialData,
                                                        const char* debugName) noexcept
        {
            rhi::RhiBufferDesc desc = {};
            desc.size = size;
            desc.usage = usage;
            desc.initialData = initialData;
            desc.debugName = debugName;
            return desc;
        }

        [[nodiscard]] RTMaterialUniformData ToUniformData(const RTMaterialResourceDesc& desc) noexcept
        {
            RTMaterialUniformData data = {};
            data.baseColor[0] = desc.baseColor.GetX();
            data.baseColor[1] = desc.baseColor.GetY();
            data.baseColor[2] = desc.baseColor.GetZ();
            data.baseColor[3] = desc.baseColor.GetW();
            return data;
        }
    } // namespace

    RTMeshResource::RTMeshResource(RTMeshResourceDesc desc)
        : desc_(std::move(desc))
    {
    }

    const RTMeshResourceDesc& RTMeshResource::GetDesc() const noexcept
    {
        return desc_;
    }

    bool RTMeshResource::IsInitialized() const noexcept
    {
        return vertexBuffer_ != nullptr;
    }

    rhi::RhiBuffer* RTMeshResource::GetVertexBuffer() noexcept
    {
        return vertexBuffer_.get();
    }

    const rhi::RhiBuffer* RTMeshResource::GetVertexBuffer() const noexcept
    {
        return vertexBuffer_.get();
    }

    rhi::RhiBuffer* RTMeshResource::GetIndexBuffer() noexcept
    {
        return indexBuffer_.get();
    }

    const rhi::RhiBuffer* RTMeshResource::GetIndexBuffer() const noexcept
    {
        return indexBuffer_.get();
    }

    UInt32 RTMeshResource::GetVertexStride() const noexcept
    {
        return static_cast<UInt32>(sizeof(RTMeshVertex));
    }

    UInt32 RTMeshResource::GetVertexCount() const noexcept
    {
        return static_cast<UInt32>(desc_.vertices.size());
    }

    UInt32 RTMeshResource::GetIndexCount() const noexcept
    {
        return static_cast<UInt32>(desc_.indices.size());
    }

    void RTMeshResource::InitRenderResource(rhi::RhiDevice& device, RTMeshResourceDesc desc)
    {
        VE_ASSERT_RENDER_THREAD();

        ResetRenderResource();
        desc_ = std::move(desc);

        if (desc_.vertices.empty())
        {
            return;
        }

        const UInt64 vertexBufferSize = static_cast<UInt64>(desc_.vertices.size() * sizeof(RTMeshVertex));
        vertexBuffer_ = device.CreateBuffer(MakeBufferDesc(vertexBufferSize,
                                                           rhi::RhiBufferUsage::Vertex,
                                                           desc_.vertices.data(),
                                                           "RTMeshResourceVertexBuffer"));
        VE_ASSERT_MESSAGE(vertexBuffer_ != nullptr, "RTMeshResource failed to create vertex buffer.");

        if (desc_.indices.empty())
        {
            return;
        }

        const UInt64 indexBufferSize = static_cast<UInt64>(desc_.indices.size() * sizeof(UInt32));
        indexBuffer_ = device.CreateBuffer(MakeBufferDesc(indexBufferSize,
                                                          rhi::RhiBufferUsage::Index,
                                                          desc_.indices.data(),
                                                          "RTMeshResourceIndexBuffer"));
        VE_ASSERT_MESSAGE(indexBuffer_ != nullptr, "RTMeshResource failed to create index buffer.");
    }

    void RTMeshResource::ResetRenderResource() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        indexBuffer_.reset();
        vertexBuffer_.reset();
    }

    RTMaterialResource::RTMaterialResource(RTMaterialResourceDesc desc)
        : desc_(std::move(desc))
    {
    }

    const RTMaterialResourceDesc& RTMaterialResource::GetDesc() const noexcept
    {
        return desc_;
    }

    bool RTMaterialResource::IsInitialized() const noexcept
    {
        return uniformBuffer_ != nullptr;
    }

    rhi::RhiBuffer* RTMaterialResource::GetUniformBuffer() noexcept
    {
        return uniformBuffer_.get();
    }

    const rhi::RhiBuffer* RTMaterialResource::GetUniformBuffer() const noexcept
    {
        return uniformBuffer_.get();
    }

    void RTMaterialResource::InitRenderResource(rhi::RhiDevice& device, RTMaterialResourceDesc desc)
    {
        VE_ASSERT_RENDER_THREAD();

        ResetRenderResource();
        desc_ = std::move(desc);

        const RTMaterialUniformData uniformData = ToUniformData(desc_);
        uniformBuffer_ = device.CreateBuffer(MakeBufferDesc(sizeof(RTMaterialUniformData),
                                                            rhi::RhiBufferUsage::Uniform,
                                                            &uniformData,
                                                            "RTMaterialResourceUniformBuffer"));
        VE_ASSERT_MESSAGE(uniformBuffer_ != nullptr, "RTMaterialResource failed to create uniform buffer.");
    }

    void RTMaterialResource::ResetRenderResource() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        uniformBuffer_.reset();
    }
} // namespace ve
