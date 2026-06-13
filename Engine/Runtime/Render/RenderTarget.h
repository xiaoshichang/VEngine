#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Platform/Window.h"

#include <string>

namespace ve
{
    enum class RenderTargetKind
    {
        Texture,
        Window,
    };

    struct RenderTargetDesc
    {
        std::string name = "RenderTarget";
        RenderTargetKind kind = RenderTargetKind::Texture;
        WindowExtent extent = {};
        rhi::RhiFormat colorFormat = rhi::RhiFormat::Bgra8Unorm;
    };

    class RenderTarget
    {
    public:
        RenderTarget();
        explicit RenderTarget(RenderTargetDesc desc);

        [[nodiscard]] bool IsValid() const noexcept;

        [[nodiscard]] const std::string& GetName() const noexcept;
        void SetName(std::string name);

        [[nodiscard]] RenderTargetKind GetKind() const noexcept;
        void SetKind(RenderTargetKind kind) noexcept;

        [[nodiscard]] WindowExtent GetExtent() const noexcept;
        void SetExtent(WindowExtent extent) noexcept;

        [[nodiscard]] rhi::RhiFormat GetColorFormat() const noexcept;
        void SetColorFormat(rhi::RhiFormat colorFormat) noexcept;

        void Reset() noexcept;

    private:
        std::string name_ = "RenderTarget";
        RenderTargetKind kind_ = RenderTargetKind::Texture;
        WindowExtent extent_ = {};
        rhi::RhiFormat colorFormat_ = rhi::RhiFormat::Bgra8Unorm;
    };
} // namespace ve
