#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderResourceLifetime.h"
#include "Engine/Runtime/Render/RenderUniformBuffer.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace ve
{
    class FrameTransientResourcePool final : public NonCopyable
    {
    public:
        void Initialize(rhi::RhiDevice& device) noexcept;
        void BeginFrame() noexcept;
        [[nodiscard]] std::shared_ptr<rhi::RhiTexture> AcquireTexture(const rhi::RhiTextureDesc& desc);
        void ReleaseTexture(const rhi::RhiTextureDesc& desc, std::shared_ptr<rhi::RhiTexture> texture);
        [[nodiscard]] UniformBufferAllocation UploadUniform(const void* data, UInt64 size, const char* debugName);
        void Adopt(std::shared_ptr<rhi::RhiObject> object);
        [[nodiscard]] RhiObjectList Shutdown() noexcept;

    private:
        struct TransientTextureKey
        {
            rhi::RhiTextureDimension dimension = rhi::RhiTextureDimension::Texture2D;
            UInt32 width = 0;
            UInt32 height = 0;
            UInt32 depth = 1;
            UInt32 mipLevelCount = 1;
            rhi::RhiFormat format = rhi::RhiFormat::Rgba8Unorm;
            rhi::RhiTextureUsage usage = rhi::RhiTextureUsage::Sampled;

            [[nodiscard]] bool operator==(const TransientTextureKey&) const noexcept = default;
        };

        struct TransientTextureKeyHash
        {
            [[nodiscard]] SizeT operator()(const TransientTextureKey& key) const noexcept;
        };

        struct UsedUniformBuffer
        {
            UInt64 size = 0;
            std::shared_ptr<rhi::RhiBuffer> buffer;
        };

        [[nodiscard]] static TransientTextureKey BuildTextureKey(const rhi::RhiTextureDesc& desc) noexcept;

        rhi::RhiDevice* device_ = nullptr;
        std::unordered_map<TransientTextureKey, std::vector<std::shared_ptr<rhi::RhiTexture>>, TransientTextureKeyHash> availableTextures_;
        std::unordered_map<UInt64, std::vector<std::shared_ptr<rhi::RhiBuffer>>> availableUniformBuffers_;
        std::vector<UsedUniformBuffer> usedUniformBuffers_;
        RhiObjectList adoptedObjects_;
    };
} // namespace ve
