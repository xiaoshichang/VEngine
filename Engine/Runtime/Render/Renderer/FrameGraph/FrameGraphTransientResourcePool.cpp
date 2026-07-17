#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphTransientResourcePool.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

namespace ve
{
    void FrameGraphTransientResourcePool::Initialize(rhi::RhiDevice& device) noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(device_ == nullptr);
        VE_ASSERT(availableTextures_.empty());
        device_ = &device;
    }

    std::unique_ptr<rhi::RhiTexture> FrameGraphTransientResourcePool::AcquireTexture(const FrameGraphTextureDesc& desc, const char* debugName)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(device_ != nullptr);

        auto bucketIt = availableTextures_.find(desc);
        if (bucketIt != availableTextures_.end() && !bucketIt->second.empty())
        {
            std::unique_ptr<rhi::RhiTexture> texture = std::move(bucketIt->second.back());
            bucketIt->second.pop_back();
            return texture;
        }

        return device_->CreateTexture(BuildRhiTextureDesc(desc, debugName));
    }

    void FrameGraphTransientResourcePool::ReleaseTexture(const FrameGraphTextureDesc& desc, std::unique_ptr<rhi::RhiTexture> texture)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(device_ != nullptr);
        VE_ASSERT(texture != nullptr);
        availableTextures_[desc].push_back(std::move(texture));
    }

    void FrameGraphTransientResourcePool::Shutdown() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        availableTextures_.clear();
        device_ = nullptr;
    }
} // namespace ve
