#include "Engine/Runtime/Render/RenderResource.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] rhi::RhiBufferDesc MakeBufferDesc(UInt64 size, rhi::RhiBufferUsage usage, const void* initialData, const char* debugName) noexcept
        {
            rhi::RhiBufferDesc desc = {};
            desc.size = size;
            desc.usage = usage;
            desc.initialData = initialData;
            desc.debugName = debugName;
            return desc;
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
        vertexBuffer_ = device.CreateBuffer(MakeBufferDesc(vertexBufferSize, rhi::RhiBufferUsage::Vertex, desc_.vertices.data(), "RTMeshResourceVertexBuffer"));
        VE_ASSERT_MESSAGE(vertexBuffer_ != nullptr, "RTMeshResource failed to create vertex buffer.");

        if (desc_.indices.empty())
        {
            return;
        }

        const UInt64 indexBufferSize = static_cast<UInt64>(desc_.indices.size() * sizeof(UInt32));
        indexBuffer_ = device.CreateBuffer(MakeBufferDesc(indexBufferSize, rhi::RhiBufferUsage::Index, desc_.indices.data(), "RTMeshResourceIndexBuffer"));
        VE_ASSERT_MESSAGE(indexBuffer_ != nullptr, "RTMeshResource failed to create index buffer.");
    }

    void RTMeshResource::ResetRenderResource() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        indexBuffer_.reset();
        vertexBuffer_.reset();
    }

    RTShaderResource::RTShaderResource(RTShaderResourceDesc desc)
        : desc_(std::move(desc))
    {
    }

    const RTShaderResourceDesc& RTShaderResource::GetDesc() const noexcept
    {
        return desc_;
    }

    bool RTShaderResource::IsInitialized() const noexcept
    {
        return vertexShader_ != nullptr || fragmentShader_ != nullptr;
    }

    rhi::RhiShaderModule* RTShaderResource::GetVertexShader() noexcept
    {
        return vertexShader_.get();
    }

    const rhi::RhiShaderModule* RTShaderResource::GetVertexShader() const noexcept
    {
        return vertexShader_.get();
    }

    rhi::RhiShaderModule* RTShaderResource::GetFragmentShader() noexcept
    {
        return fragmentShader_.get();
    }

    const rhi::RhiShaderModule* RTShaderResource::GetFragmentShader() const noexcept
    {
        return fragmentShader_.get();
    }

    void RTShaderResource::InitRenderResource(rhi::RhiDevice& device, RTShaderResourceDesc desc)
    {
        VE_ASSERT_RENDER_THREAD();

        ResetRenderResource();
        desc_ = std::move(desc);

        for (const RTShaderStageResourceDesc& stageDesc : desc_.stages)
        {
            rhi::RhiShaderModuleDesc shaderDesc = {};
            shaderDesc.stage = stageDesc.stage;
            shaderDesc.entryPoint = stageDesc.entryPoint.c_str();
            shaderDesc.debugName = stageDesc.debugName.c_str();

            const rhi::RhiBackend backend = device.GetBackend();
            if (backend == rhi::RhiBackend::D3D11 && !stageDesc.d3d11Bytecode.empty())
            {
                shaderDesc.codeFormat = rhi::RhiShaderCodeFormat::Bytecode;
                shaderDesc.bytecode = stageDesc.d3d11Bytecode.data();
                shaderDesc.bytecodeSize = static_cast<UInt64>(stageDesc.d3d11Bytecode.size());
            }
            else if (backend == rhi::RhiBackend::D3D12 && !stageDesc.d3d12Bytecode.empty())
            {
                shaderDesc.codeFormat = rhi::RhiShaderCodeFormat::Bytecode;
                shaderDesc.bytecode = stageDesc.d3d12Bytecode.data();
                shaderDesc.bytecodeSize = static_cast<UInt64>(stageDesc.d3d12Bytecode.size());
            }
            else if (backend == rhi::RhiBackend::Metal && !stageDesc.metalSource.empty())
            {
                shaderDesc.codeFormat = rhi::RhiShaderCodeFormat::Source;
                shaderDesc.source = stageDesc.metalSource.c_str();
            }
            else
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "RTShaderResource does not have an artifact for the active backend.");
                continue;
            }

            std::unique_ptr<rhi::RhiShaderModule> shader = device.CreateShaderModule(shaderDesc);
            VE_ASSERT_MESSAGE(shader != nullptr, "RTShaderResource failed to create shader module.");

            if (stageDesc.stage == rhi::RhiShaderStage::Vertex)
            {
                vertexShader_ = std::move(shader);
            }
            else if (stageDesc.stage == rhi::RhiShaderStage::Fragment)
            {
                fragmentShader_ = std::move(shader);
            }
        }
    }

    void RTShaderResource::ResetRenderResource() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        fragmentShader_.reset();
        vertexShader_.reset();
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
        return uniformAllocation_.buffer != nullptr;
    }

    rhi::RhiBuffer* RTMaterialResource::GetUniformBuffer() noexcept
    {
        return uniformAllocation_.buffer;
    }

    const rhi::RhiBuffer* RTMaterialResource::GetUniformBuffer() const noexcept
    {
        return uniformAllocation_.buffer;
    }

    UInt64 RTMaterialResource::GetUniformBufferOffset() const noexcept
    {
        return uniformAllocation_.offset;
    }

    UInt64 RTMaterialResource::GetUniformBufferSize() const noexcept
    {
        return uniformAllocation_.size;
    }

    std::shared_ptr<RTShaderResource> RTMaterialResource::GetShaderResource() const noexcept
    {
        return desc_.shaderResource;
    }

    UInt64 RTMaterialResource::GetRevision() const noexcept
    {
        return desc_.revision;
    }

    void RTMaterialResource::InitRenderResource(MaterialUniformPool& uniformPool, RTMaterialResourceDesc desc)
    {
        VE_ASSERT_RENDER_THREAD();

        if (desc.constantData.empty())
        {
            ResetRenderResource(uniformPool);
            desc_ = std::move(desc);
            return;
        }

        const UInt64 constantDataSize = static_cast<UInt64>(desc.constantData.size());
        if (!uniformPool.IsValid(uniformAllocation_))
        {
            uniformAllocation_ = {};
        }
        if (uniformAllocation_.buffer == nullptr || uniformAllocation_.size != constantDataSize)
        {
            MaterialUniformAllocation newAllocation = uniformPool.Allocate(constantDataSize);
            uniformPool.Update(newAllocation, desc.constantData.data(), constantDataSize);
            uniformPool.Release(uniformAllocation_);
            uniformAllocation_ = newAllocation;
        }
        else
        {
            uniformPool.Update(uniformAllocation_, desc.constantData.data(), constantDataSize);
        }
        desc_ = std::move(desc);
    }

    void RTMaterialResource::ResetRenderResource(MaterialUniformPool& uniformPool)
    {
        VE_ASSERT_RENDER_THREAD();
        uniformPool.Release(uniformAllocation_);
    }
} // namespace ve
