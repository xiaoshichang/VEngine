#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h"

#include <functional>

namespace ve
{
    namespace
    {
        template<typename T>
        void HashCombine(SizeT& seed, T value) noexcept
        {
            const SizeT hash = std::hash<T>{}(value);
            seed ^= hash + static_cast<SizeT>(0x9e3779b9u) + (seed << 6u) + (seed >> 2u);
        }
    } // namespace

    SizeT FrameGraphTextureDescHash::operator()(const FrameGraphTextureDesc& desc) const noexcept
    {
        SizeT result = 0;
        HashCombine(result, static_cast<UInt32>(desc.dimension));
        HashCombine(result, desc.width);
        HashCombine(result, desc.height);
        HashCombine(result, desc.depth);
        HashCombine(result, desc.mipLevelCount);
        HashCombine(result, static_cast<UInt32>(desc.format));
        HashCombine(result, static_cast<UInt32>(desc.usage));
        return result;
    }

    rhi::RhiTextureDesc BuildRhiTextureDesc(const FrameGraphTextureDesc& desc, const char* debugName) noexcept
    {
        rhi::RhiTextureDesc rhiDesc = {};
        rhiDesc.dimension = desc.dimension;
        rhiDesc.width = desc.width;
        rhiDesc.height = desc.height;
        rhiDesc.depth = desc.depth;
        rhiDesc.mipLevelCount = desc.mipLevelCount;
        rhiDesc.format = desc.format;
        rhiDesc.usage = desc.usage;
        rhiDesc.debugName = debugName;
        return rhiDesc;
    }
} // namespace ve
