#include "Engine/Runtime/Render/RenderTarget.h"

#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] rhi::RhiTextureUsage MakeRenderTargetTextureUsage() noexcept
        {
            const auto sampled = static_cast<UInt32>(rhi::RhiTextureUsage::Sampled);
            const auto renderTarget = static_cast<UInt32>(rhi::RhiTextureUsage::RenderTarget);
            return static_cast<rhi::RhiTextureUsage>(sampled | renderTarget);
        }

        [[nodiscard]] RenderTargetDesc BuildDesc(const std::string& name,
                                                 RenderTargetKind kind,
                                                 WindowExtent extent,
                                                 rhi::RhiFormat colorFormat)
        {
            RenderTargetDesc desc = {};
            desc.name = name;
            desc.kind = kind;
            desc.extent = extent;
            desc.colorFormat = colorFormat;
            return desc;
        }
    } // namespace

    RenderTarget::RenderTarget() = default;

    RenderTarget::RenderTarget(RenderTargetDesc desc)
        : name_(std::move(desc.name))
        , kind_(desc.kind)
        , extent_(desc.extent)
        , colorFormat_(desc.colorFormat)
    {
    }

    bool RenderTarget::IsValid() const noexcept
    {
        return extent_.width != 0 && extent_.height != 0;
    }

    const std::string& RenderTarget::GetName() const noexcept
    {
        return name_;
    }

    void RenderTarget::SetName(std::string name)
    {
        name_ = std::move(name);
    }

    RenderTargetKind RenderTarget::GetKind() const noexcept
    {
        return kind_;
    }

    void RenderTarget::SetKind(RenderTargetKind kind) noexcept
    {
        kind_ = kind;
    }

    WindowExtent RenderTarget::GetExtent() const noexcept
    {
        return extent_;
    }

    void RenderTarget::SetExtent(WindowExtent extent) noexcept
    {
        extent_ = extent;
    }

    rhi::RhiFormat RenderTarget::GetColorFormat() const noexcept
    {
        return colorFormat_;
    }

    void RenderTarget::SetColorFormat(rhi::RhiFormat colorFormat) noexcept
    {
        colorFormat_ = colorFormat;
    }

    ErrorCode RenderTarget::InitRenderResource(RenderSystem& renderSystem)
    {
        VE_ASSERT_SCENE_THREAD();

        if (!IsValid())
        {
            return ErrorCode::InvalidArgument;
        }

        EnsureRenderThreadProxy();
        return renderSystem.InitRenderResource(rtRenderTarget_, BuildDesc(name_, kind_, extent_, colorFormat_));
    }

    std::shared_ptr<RTRenderTarget> RenderTarget::GetRTRenderTarget() const noexcept
    {
        return rtRenderTarget_;
    }

    void RenderTarget::Reset() noexcept
    {
        name_ = "RenderTarget";
        kind_ = RenderTargetKind::Texture;
        extent_ = {};
        colorFormat_ = rhi::RhiFormat::Bgra8Unorm;
        rtRenderTarget_.reset();
    }

    void RenderTarget::EnsureRenderThreadProxy()
    {
        if (rtRenderTarget_ == nullptr)
        {
            rtRenderTarget_ = std::make_shared<RTRenderTarget>(BuildDesc(name_, kind_, extent_, colorFormat_));
        }
    }

    RTRenderTarget::RTRenderTarget(RenderTargetDesc desc)
        : desc_(std::move(desc))
    {
    }

    const RenderTargetDesc& RTRenderTarget::GetDesc() const noexcept
    {
        return desc_;
    }

    bool RTRenderTarget::IsInitialized() const noexcept
    {
        if (desc_.kind == RenderTargetKind::Window)
        {
            return true;
        }

        return texture_ != nullptr;
    }

    rhi::RhiTexture* RTRenderTarget::GetTexture() noexcept
    {
        return texture_.get();
    }

    const rhi::RhiTexture* RTRenderTarget::GetTexture() const noexcept
    {
        return texture_.get();
    }

    void* RTRenderTarget::GetNativeSampledViewHandle() const noexcept
    {
        return nativeSampledViewHandle_.load(std::memory_order_acquire);
    }

    ErrorCode RTRenderTarget::InitRenderResource(rhi::RhiDevice& device, RenderTargetDesc desc)
    {
        VE_ASSERT_RENDER_THREAD();

        const bool textureMatchesDesc = texture_ != nullptr && desc.kind == RenderTargetKind::Texture &&
                                        texture_->GetWidth() == desc.extent.width &&
                                        texture_->GetHeight() == desc.extent.height &&
                                        texture_->GetFormat() == desc.colorFormat;

        desc_ = std::move(desc);
        if (!textureMatchesDesc)
        {
            texture_.reset();
            nativeSampledViewHandle_.store(nullptr, std::memory_order_release);
        }

        if (desc_.extent.width == 0 || desc_.extent.height == 0)
        {
            return ErrorCode::InvalidArgument;
        }

        if (desc_.kind == RenderTargetKind::Window)
        {
            texture_.reset();
            nativeSampledViewHandle_.store(nullptr, std::memory_order_release);
            return ErrorCode::None;
        }

        if (IsInitialized())
        {
            return ErrorCode::None;
        }

        rhi::RhiTextureDesc textureDesc = {};
        textureDesc.dimension = rhi::RhiTextureDimension::Texture2D;
        textureDesc.width = desc_.extent.width;
        textureDesc.height = desc_.extent.height;
        textureDesc.depth = 1;
        textureDesc.mipLevelCount = 1;
        textureDesc.format = desc_.colorFormat;
        textureDesc.usage = MakeRenderTargetTextureUsage();
        textureDesc.debugName = desc_.name.c_str();

        texture_ = device.CreateTexture(textureDesc);
        if (texture_ == nullptr)
        {
            return ErrorCode::PlatformError;
        }

        nativeSampledViewHandle_.store(texture_->GetNativeSampledViewHandle(), std::memory_order_release);
        return ErrorCode::None;
    }

    void RTRenderTarget::ResetRenderResource() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        texture_.reset();
        nativeSampledViewHandle_.store(nullptr, std::memory_order_release);
    }
} // namespace ve
