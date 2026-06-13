#include "Engine/Runtime/Render/RenderTarget.h"

#include <utility>

namespace ve
{
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

    void RenderTarget::Reset() noexcept
    {
        name_ = "RenderTarget";
        kind_ = RenderTargetKind::Texture;
        extent_ = {};
        colorFormat_ = rhi::RhiFormat::Bgra8Unorm;
    }
} // namespace ve
