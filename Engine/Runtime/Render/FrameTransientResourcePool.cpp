#include "Engine/Runtime/Render/FrameTransientResourcePool.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <functional>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] constexpr UInt64 AlignUp(UInt64 value, UInt64 alignment) noexcept
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        template<typename TValue>
        void HashCombine(SizeT& hash, TValue value) noexcept
        {
            hash ^= std::hash<TValue>{}(value) + static_cast<SizeT>(0x9e3779b9u) + (hash << 6u) + (hash >> 2u);
        }
    } // namespace

    void FrameTransientResourcePool::Initialize(rhi::RhiDevice& device) noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(device_ == nullptr);
        device_ = &device;
    }

    void FrameTransientResourcePool::BeginFrame() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(device_ != nullptr);
        for (UsedUniformBuffer& used : usedUniformBuffers_)
        {
            availableUniformBuffers_[used.size].push_back(std::move(used.buffer));
        }
        usedUniformBuffers_.clear();
        adoptedObjects_.clear();
    }

    std::shared_ptr<rhi::RhiTexture> FrameTransientResourcePool::AcquireTexture(const rhi::RhiTextureDesc& desc)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(device_ != nullptr);
        VE_ASSERT(desc.initialData == nullptr);
        const TransientTextureKey key = BuildTextureKey(desc);
        std::vector<std::shared_ptr<rhi::RhiTexture>>& available = availableTextures_[key];
        if (!available.empty())
        {
            std::shared_ptr<rhi::RhiTexture> texture = std::move(available.back());
            available.pop_back();
            return texture;
        }
        return device_->CreateTexture(desc);
    }

    void FrameTransientResourcePool::ReleaseTexture(const rhi::RhiTextureDesc& desc, std::shared_ptr<rhi::RhiTexture> texture)
    {
        VE_ASSERT_RENDER_THREAD();
        if (texture != nullptr)
        {
            availableTextures_[BuildTextureKey(desc)].push_back(std::move(texture));
        }
    }

    UniformBufferAllocation FrameTransientResourcePool::UploadUniform(const void* data, UInt64 size, const char* debugName)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(device_ != nullptr);
        VE_ASSERT(data != nullptr);
        VE_ASSERT(size > 0);
        if (data == nullptr || size == 0)
        {
            return {};
        }

        constexpr UInt64 UniformAlignment = 256;
        const UInt64 alignedSize = AlignUp(size, UniformAlignment);
        std::vector<std::shared_ptr<rhi::RhiBuffer>>& available = availableUniformBuffers_[alignedSize];
        std::shared_ptr<rhi::RhiBuffer> buffer;
        if (!available.empty())
        {
            buffer = std::move(available.back());
            available.pop_back();
        }
        else
        {
            rhi::RhiBufferDesc desc = {};
            desc.size = alignedSize;
            desc.usage = rhi::RhiBufferUsage::Uniform;
            desc.memoryUsage = rhi::RhiBufferMemoryUsage::CpuToGpu;
            desc.debugName = debugName;
            buffer = device_->CreateBuffer(desc);
        }
        VE_ASSERT_MESSAGE(buffer != nullptr, "Failed to create a transient uniform buffer.");
        if (buffer == nullptr)
        {
            return {};
        }

        device_->UpdateBuffer(*buffer, 0, data, size, rhi::RhiBufferUpdateMode::Discard);
        rhi::RhiBuffer* result = buffer.get();
        usedUniformBuffers_.push_back(UsedUniformBuffer{alignedSize, std::move(buffer)});
        return UniformBufferAllocation{result, 0, size};
    }

    void FrameTransientResourcePool::Adopt(std::shared_ptr<rhi::RhiObject> object)
    {
        VE_ASSERT_RENDER_THREAD();
        if (object != nullptr)
        {
            adoptedObjects_.push_back(std::move(object));
        }
    }

    RhiObjectList FrameTransientResourcePool::Shutdown() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        RhiObjectList objects;
        for (auto& [key, textures] : availableTextures_)
        {
            static_cast<void>(key);
            for (std::shared_ptr<rhi::RhiTexture>& texture : textures)
            {
                MoveRhiObject(objects, texture);
            }
        }
        for (auto& [size, buffers] : availableUniformBuffers_)
        {
            static_cast<void>(size);
            for (std::shared_ptr<rhi::RhiBuffer>& buffer : buffers)
            {
                MoveRhiObject(objects, buffer);
            }
        }
        for (UsedUniformBuffer& used : usedUniformBuffers_)
        {
            MoveRhiObject(objects, used.buffer);
        }
        for (std::shared_ptr<rhi::RhiObject>& object : adoptedObjects_)
        {
            MoveRhiObject(objects, object);
        }

        availableTextures_.clear();
        availableUniformBuffers_.clear();
        usedUniformBuffers_.clear();
        adoptedObjects_.clear();
        device_ = nullptr;
        return objects;
    }

    SizeT FrameTransientResourcePool::TransientTextureKeyHash::operator()(const TransientTextureKey& key) const noexcept
    {
        SizeT hash = 0;
        HashCombine(hash, static_cast<UInt32>(key.dimension));
        HashCombine(hash, key.width);
        HashCombine(hash, key.height);
        HashCombine(hash, key.depth);
        HashCombine(hash, key.mipLevelCount);
        HashCombine(hash, static_cast<UInt32>(key.format));
        HashCombine(hash, static_cast<UInt32>(key.usage));
        return hash;
    }

    FrameTransientResourcePool::TransientTextureKey FrameTransientResourcePool::BuildTextureKey(const rhi::RhiTextureDesc& desc) noexcept
    {
        return TransientTextureKey{desc.dimension, desc.width, desc.height, desc.depth, desc.mipLevelCount, desc.format, desc.usage};
    }
} // namespace ve
