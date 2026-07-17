#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace ve
{
    /// Reuses graph-owned textures within one fence-protected in-flight frame context.
    class FrameGraphTransientResourcePool final : public NonCopyable
    {
    public:
        void Initialize(rhi::RhiDevice& device) noexcept;
        [[nodiscard]] std::unique_ptr<rhi::RhiTexture> AcquireTexture(const FrameGraphTextureDesc& desc, const char* debugName);
        void ReleaseTexture(const FrameGraphTextureDesc& desc, std::unique_ptr<rhi::RhiTexture> texture);
        void Shutdown() noexcept;

    private:
        rhi::RhiDevice* device_ = nullptr;
        std::unordered_map<FrameGraphTextureDesc,
                           std::vector<std::unique_ptr<rhi::RhiTexture>>,
                           FrameGraphTextureDescHash>
            availableTextures_;
    };
} // namespace ve
