#include "Engine/Runtime/Render/RenderResource.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <iterator>
#include <limits>
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

    std::shared_ptr<rhi::RhiBuffer> RTMeshResource::GetVertexBufferShared() const noexcept
    {
        return vertexBuffer_;
    }

    rhi::RhiBuffer* RTMeshResource::GetIndexBuffer() noexcept
    {
        return indexBuffer_.get();
    }

    const rhi::RhiBuffer* RTMeshResource::GetIndexBuffer() const noexcept
    {
        return indexBuffer_.get();
    }

    std::shared_ptr<rhi::RhiBuffer> RTMeshResource::GetIndexBufferShared() const noexcept
    {
        return indexBuffer_;
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

    RhiObjectList RTMeshResource::TakeRhiObjects() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        RhiObjectList objects;
        objects.reserve(2);
        MoveRhiObject(objects, vertexBuffer_);
        MoveRhiObject(objects, indexBuffer_);
        return objects;
    }

    void RTMeshResource::InitRenderResource(rhi::RhiDevice& device, RTMeshResourceDesc desc)
    {
        VE_ASSERT_RENDER_THREAD();

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

    RTTextureResource::RTTextureResource(RTTextureResourceDesc desc)
        : desc_(std::move(desc))
    {
    }

    const RTTextureResourceDesc& RTTextureResource::GetDesc() const noexcept
    {
        return desc_;
    }

    bool RTTextureResource::IsInitialized() const noexcept
    {
        return texture_ != nullptr;
    }

    rhi::RhiTexture* RTTextureResource::GetTexture() noexcept
    {
        return texture_.get();
    }

    const rhi::RhiTexture* RTTextureResource::GetTexture() const noexcept
    {
        return texture_.get();
    }

    RhiObjectList RTTextureResource::TakeRhiObjects() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        RhiObjectList objects;
        MoveRhiObject(objects, texture_);
        return objects;
    }

    void RTTextureResource::InitRenderResource(rhi::RhiDevice& device, RTTextureResourceDesc desc)
    {
        VE_ASSERT_RENDER_THREAD();

        desc_ = std::move(desc);
        if (desc_.width == 0 || desc_.height == 0)
        {
            return;
        }

        rhi::RhiTextureDesc textureDesc = {};
        textureDesc.dimension = rhi::RhiTextureDimension::Texture2D;
        textureDesc.width = desc_.width;
        textureDesc.height = desc_.height;
        textureDesc.depth = desc_.depth;
        textureDesc.mipLevelCount = desc_.mipLevelCount;
        textureDesc.format = desc_.format;
        textureDesc.usage = desc_.usage;
        textureDesc.debugName = desc_.name.c_str();
        if (!desc_.initialData.empty() && desc_.initialDataRowPitch != 0)
        {
            textureDesc.initialData = desc_.initialData.data();
            textureDesc.initialDataSize = desc_.initialData.size();
            textureDesc.initialDataRowPitch = desc_.initialDataRowPitch;
        }

        texture_ = device.CreateTexture(textureDesc);
        VE_ASSERT_MESSAGE(texture_ != nullptr, "RTTextureResource failed to create texture.");
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
        return !passes_.empty();
    }

    RTShaderPass* RTShaderResource::GetPass(ShaderPassType type) noexcept
    {
        for (const auto& pass : passes_)
        {
            if (pass->GetType() == type) return pass.get();
        }
        return nullptr;
    }

    const RTShaderPass* RTShaderResource::GetPass(ShaderPassType type) const noexcept
    {
        for (const auto& pass : passes_)
        {
            if (pass->GetType() == type) return pass.get();
        }
        return nullptr;
    }

    RTShaderPass* RTShaderResource::GetPass(std::string_view name) noexcept
    {
        for (const auto& pass : passes_)
        {
            if (pass->GetName() == name) return pass.get();
        }
        return nullptr;
    }

    const RTShaderPass* RTShaderResource::GetPass(std::string_view name) const noexcept
    {
        for (const auto& pass : passes_)
        {
            if (pass->GetName() == name) return pass.get();
        }
        return nullptr;
    }

    bool RTShaderResource::HasPass(ShaderPassType type) const noexcept
    {
        return GetPass(type) != nullptr;
    }

    UInt64 RTShaderResource::GetRevision() const noexcept
    {
        return revision_;
    }

    RhiObjectList RTShaderPass::TakeRhiObjects() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        RhiObjectList objects;
        objects.reserve(3);
        MoveRhiObject(objects, vertexShader_);
        MoveRhiObject(objects, fragmentShader_);
        MoveRhiObject(objects, computeShader_);
        return objects;
    }

    RhiObjectList RTShaderResource::TakeRhiObjects() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        RhiObjectList objects;
        for (std::unique_ptr<RTShaderPass>& pass : passes_)
        {
            RhiObjectList passObjects = pass->TakeRhiObjects();
            objects.insert(objects.end(), std::make_move_iterator(passObjects.begin()), std::make_move_iterator(passObjects.end()));
        }
        passes_.clear();
        return objects;
    }

    void RTShaderResource::InitRenderResource(rhi::RhiDevice& device, RTShaderResourceDesc desc)
    {
        VE_ASSERT_RENDER_THREAD();

        desc_ = std::move(desc);
        bool createdArtifact = false;

        for (const RTShaderPassResourceDesc& passDesc : desc_.passes)
        {
            auto pass = std::make_unique<RTShaderPass>(passDesc.type, passDesc.name);
            for (const RTShaderStageResourceDesc& stageDesc : passDesc.stages)
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

                std::shared_ptr<rhi::RhiShaderModule> shader = device.CreateShaderModule(shaderDesc);
                VE_ASSERT_MESSAGE(shader != nullptr, "RTShaderResource failed to create shader module.");
                if (shader == nullptr) continue;

                if (stageDesc.stage == rhi::RhiShaderStage::Vertex) pass->VertexShader() = std::move(shader);
                else if (stageDesc.stage == rhi::RhiShaderStage::Fragment) pass->FragmentShader() = std::move(shader);
                else if (stageDesc.stage == rhi::RhiShaderStage::Compute) pass->ComputeShader() = std::move(shader);
                createdArtifact = true;
            }
            passes_.push_back(std::move(pass));
        }

        if (createdArtifact)
        {
            VE_ASSERT_MESSAGE(revision_ != std::numeric_limits<UInt64>::max(), "RTShaderResource revision overflow.");
            if (revision_ != std::numeric_limits<UInt64>::max())
            {
                ++revision_;
            }
        }
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
        return desc_.shaderResource != nullptr;
    }

    std::shared_ptr<RTShaderResource> RTMaterialResource::GetShaderResource() const noexcept
    {
        return desc_.shaderResource;
    }

    UInt64 RTMaterialResource::GetRevision() const noexcept
    {
        return desc_.revision;
    }

    UniformBufferAllocation RTMaterialResource::GetUniformBuffer(rhi::RhiDevice& device, UInt32 frameSlotIndex)
    {
        VE_ASSERT_RENDER_THREAD();
        if (desc_.constantData.empty())
        {
            return {};
        }
        return uniformBuffer_.GetOrUpdate(device,
                                          frameSlotIndex,
                                          desc_.constantData.data(),
                                          static_cast<UInt64>(desc_.constantData.size()),
                                          desc_.revision,
                                          "RTMaterialResourceUniform");
    }

    RhiObjectList RTMaterialResource::TakeRhiObjects() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        RhiObjectList objects = uniformBuffer_.TakeRhiObjects();
        desc_ = {};
        return objects;
    }

    void RTMaterialResource::InitRenderResource(RTMaterialResourceDesc desc)
    {
        VE_ASSERT_RENDER_THREAD();
        desc_ = std::move(desc);
    }
} // namespace ve
