#include "Engine/Runtime/Render/RenderTexture.h"

#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] rhi::RhiTextureUsage MakeRenderTextureUsage() noexcept
        {
            const auto sampled = static_cast<UInt32>(rhi::RhiTextureUsage::Sampled);
            const auto renderTarget = static_cast<UInt32>(rhi::RhiTextureUsage::RenderTarget);
            return static_cast<rhi::RhiTextureUsage>(sampled | renderTarget);
        }

        [[nodiscard]] rhi::RhiTextureDesc MakeTextureDesc(const RenderTextureDesc& desc)
        {
            rhi::RhiTextureDesc textureDesc = {};
            textureDesc.dimension = rhi::RhiTextureDimension::Texture2D;
            textureDesc.width = desc.extent.width;
            textureDesc.height = desc.extent.height;
            textureDesc.depth = 1;
            textureDesc.mipLevelCount = 1;
            textureDesc.format = desc.colorFormat;
            textureDesc.usage = MakeRenderTextureUsage();
            textureDesc.debugName = desc.name.c_str();
            return textureDesc;
        }

        [[nodiscard]] rhi::RhiTextureDesc MakeDepthTextureDesc(const RenderTextureDesc& desc)
        {
            rhi::RhiTextureDesc textureDesc = {};
            textureDesc.dimension = rhi::RhiTextureDimension::Texture2D;
            textureDesc.width = desc.extent.width;
            textureDesc.height = desc.extent.height;
            textureDesc.depth = 1;
            textureDesc.mipLevelCount = 1;
            textureDesc.format = rhi::RhiFormat::Depth32Float;
            textureDesc.usage = rhi::RhiTextureUsage::DepthStencil;
            textureDesc.debugName = desc.name.c_str();
            return textureDesc;
        }

        [[nodiscard]] RenderTargetDesc ToRenderTargetDesc(const RenderTextureDesc& desc)
        {
            RenderTargetDesc renderTargetDesc = {};
            renderTargetDesc.name = desc.name;
            renderTargetDesc.kind = RenderTargetKind::Texture;
            renderTargetDesc.extent = desc.extent;
            renderTargetDesc.colorFormat = desc.colorFormat;
            return renderTargetDesc;
        }
    } // namespace

    RenderTexture::RenderTexture()
        : RenderTexture(RenderTextureDesc{})
    {
    }

    RenderTexture::RenderTexture(RenderTextureDesc desc)
        : desc_(std::move(desc))
        , renderTarget_(ToRenderTargetDesc(desc_))
    {
    }

    bool RenderTexture::IsValid() const noexcept
    {
        return desc_.extent.width != 0 && desc_.extent.height != 0;
    }

    const std::string& RenderTexture::GetName() const noexcept
    {
        return desc_.name;
    }

    WindowExtent RenderTexture::GetExtent() const noexcept
    {
        return desc_.extent;
    }

    rhi::RhiFormat RenderTexture::GetColorFormat() const noexcept
    {
        return desc_.colorFormat;
    }

    const RenderTarget& RenderTexture::GetRenderTarget() const noexcept
    {
        return renderTarget_;
    }

    void RenderTexture::Resize(WindowExtent extent)
    {
        desc_.extent = extent;
        renderTarget_.SetExtent(extent);
    }

    void RenderTexture::InitRenderResource(RenderSystem& renderSystem)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(IsValid(), "RenderTexture::InitRenderResource requires a valid extent.");

        EnsureRenderThreadProxy();
        renderSystem.InitRenderResource(rtRenderTexture_, BuildDesc());
    }

    std::shared_ptr<RTRenderTexture> RenderTexture::GetRTRenderTexture() const noexcept
    {
        return rtRenderTexture_;
    }

    void* RenderTexture::GetRenderResourceViewHandle() const noexcept
    {
        return rtRenderTexture_ != nullptr ? rtRenderTexture_->GetRenderResourceViewHandle() : nullptr;
    }

    void RenderTexture::EnsureRenderThreadProxy()
    {
        if (rtRenderTexture_ == nullptr)
        {
            rtRenderTexture_ = std::make_shared<RTRenderTexture>(BuildDesc());
        }
    }

    RenderTextureDesc RenderTexture::BuildDesc() const
    {
        return desc_;
    }

    RTRenderTexture::RTRenderTexture(RenderTextureDesc desc)
        : desc_(std::move(desc))
    {
    }

    const RenderTextureDesc& RTRenderTexture::GetDesc() const noexcept
    {
        return desc_;
    }

    bool RTRenderTexture::IsInitialized() const noexcept
    {
        return texture_ != nullptr;
    }

    rhi::RhiTexture* RTRenderTexture::GetTexture() noexcept
    {
        return texture_.get();
    }

    const rhi::RhiTexture* RTRenderTexture::GetTexture() const noexcept
    {
        return texture_.get();
    }

    rhi::RhiTexture* RTRenderTexture::GetDepthTexture() noexcept
    {
        return depthTexture_.get();
    }

    const rhi::RhiTexture* RTRenderTexture::GetDepthTexture() const noexcept
    {
        return depthTexture_.get();
    }

    void* RTRenderTexture::GetRenderResourceViewHandle() const noexcept
    {
        const UInt64 requestedRevision = requestedInitRevision_.load(std::memory_order_acquire);
        if (readyInitRevision_.load(std::memory_order_acquire) != requestedRevision)
        {
            return nullptr;
        }

        void* nativeHandle = nativeSampledViewHandle_.load(std::memory_order_acquire);
        if (requestedInitRevision_.load(std::memory_order_acquire) != requestedRevision)
        {
            return nullptr;
        }
        return nativeHandle;
    }

    UInt64 RTRenderTexture::RequestRenderResourceInit() noexcept
    {
        VE_ASSERT_SCENE_THREAD();

        const UInt64 requestRevision = requestedInitRevision_.fetch_add(1, std::memory_order_acq_rel) + 1;
        nativeSampledViewHandle_.store(nullptr, std::memory_order_release);
        return requestRevision;
    }

    void RTRenderTexture::InitRenderResource(rhi::RhiDevice& device,
                                             RenderTextureDesc desc,
                                             std::vector<std::unique_ptr<rhi::RhiObject>>& retiredResources,
                                             UInt64 requestRevision)
    {
        VE_ASSERT_RENDER_THREAD();

        const bool tracksSceneThreadRequest = requestRevision != 0;
        const bool textureMatchesDesc = texture_ != nullptr && texture_->GetWidth() == desc.extent.width && texture_->GetHeight() == desc.extent.height &&
                                        texture_->GetFormat() == desc.colorFormat;

        desc_ = std::move(desc);
        if (!textureMatchesDesc)
        {
            if (texture_ != nullptr)
            {
                retiredResources.push_back(std::move(texture_));
            }
            if (depthTexture_ != nullptr)
            {
                retiredResources.push_back(std::move(depthTexture_));
            }
            nativeSampledViewHandle_.store(nullptr, std::memory_order_release);
        }

        VE_ASSERT_MESSAGE(desc_.extent.width != 0 && desc_.extent.height != 0, "RTRenderTexture::InitRenderResource requires a valid extent.");

        if (texture_ == nullptr)
        {
            texture_ = device.CreateTexture(MakeTextureDesc(desc_));
            VE_ASSERT_MESSAGE(texture_ != nullptr, "RTRenderTexture failed to create RHI texture.");
        }

        if (depthTexture_ == nullptr)
        {
            depthTexture_ = device.CreateTexture(MakeDepthTextureDesc(desc_));
            VE_ASSERT_MESSAGE(depthTexture_ != nullptr, "RTRenderTexture failed to create depth texture.");
        }

        if (!tracksSceneThreadRequest || requestedInitRevision_.load(std::memory_order_acquire) == requestRevision)
        {
            nativeSampledViewHandle_.store(texture_->GetNativeSampledViewHandle(), std::memory_order_release);
            if (tracksSceneThreadRequest)
            {
                readyInitRevision_.store(requestRevision, std::memory_order_release);
            }
        }
    }

    void RTRenderTexture::ResetRenderResource(std::vector<std::unique_ptr<rhi::RhiObject>>& retiredResources) noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        if (texture_ != nullptr)
        {
            retiredResources.push_back(std::move(texture_));
        }
        if (depthTexture_ != nullptr)
        {
            retiredResources.push_back(std::move(depthTexture_));
        }
        nativeSampledViewHandle_.store(nullptr, std::memory_order_release);
        readyInitRevision_.store(0, std::memory_order_release);
    }
} // namespace ve
