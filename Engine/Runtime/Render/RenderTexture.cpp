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

    void* RTRenderTexture::GetRenderResourceViewHandle() const noexcept
    {
        return nativeSampledViewHandle_.load(std::memory_order_acquire);
    }

    void RTRenderTexture::InitRenderResource(rhi::RhiDevice& device, RenderTextureDesc desc)
    {
        VE_ASSERT_RENDER_THREAD();

        const bool textureMatchesDesc = texture_ != nullptr && texture_->GetWidth() == desc.extent.width && texture_->GetHeight() == desc.extent.height &&
                                        texture_->GetFormat() == desc.colorFormat;

        desc_ = std::move(desc);
        if (!textureMatchesDesc)
        {
            texture_.reset();
            nativeSampledViewHandle_.store(nullptr, std::memory_order_release);
        }

        VE_ASSERT_MESSAGE(desc_.extent.width != 0 && desc_.extent.height != 0, "RTRenderTexture::InitRenderResource requires a valid extent.");

        if (IsInitialized())
        {
            return;
        }

        rhi::RhiTextureDesc textureDesc = {};
        textureDesc.dimension = rhi::RhiTextureDimension::Texture2D;
        textureDesc.width = desc_.extent.width;
        textureDesc.height = desc_.extent.height;
        textureDesc.depth = 1;
        textureDesc.mipLevelCount = 1;
        textureDesc.format = desc_.colorFormat;
        textureDesc.usage = MakeRenderTextureUsage();
        textureDesc.debugName = desc_.name.c_str();

        texture_ = device.CreateTexture(textureDesc);
        VE_ASSERT_MESSAGE(texture_ != nullptr, "RTRenderTexture failed to create RHI texture.");

        nativeSampledViewHandle_.store(texture_->GetNativeSampledViewHandle(), std::memory_order_release);
    }

    void RTRenderTexture::ResetRenderResource() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        texture_.reset();
        nativeSampledViewHandle_.store(nullptr, std::memory_order_release);
    }
} // namespace ve
