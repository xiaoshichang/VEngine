#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/FrameUniformAllocator.h"

#include <functional>
#include <unordered_map>

namespace ve
{
    class RTCamera;
    class RTRenderItem;
    class RTScene;

    /// Lazily uploads each scene, camera, and render-item uniform block once per frame context use.
    class RenderFrameUniformCache final : public NonCopyable
    {
    public:
        void Initialize(FrameUniformAllocator& allocator) noexcept;
        void Reset() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] UniformBufferAllocation GetFrameUniform(const RTScene& scene);
        [[nodiscard]] UniformBufferAllocation GetViewUniform(const RTCamera* camera, rhi::RhiExtent2D targetExtent);
        [[nodiscard]] UniformBufferAllocation GetObjectUniform(const RTRenderItem& item);

    private:
        struct ViewUniformKey
        {
            const RTCamera* camera = nullptr;
            UInt32 width = 0;
            UInt32 height = 0;

            [[nodiscard]] bool operator==(const ViewUniformKey& other) const noexcept = default;
        };

        struct ViewUniformKeyHash
        {
            [[nodiscard]] std::size_t operator()(const ViewUniformKey& key) const noexcept
            {
                std::size_t hash = std::hash<const RTCamera*>{}(key.camera);
                hash ^= std::hash<UInt32>{}(key.width) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
                hash ^= std::hash<UInt32>{}(key.height) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
                return hash;
            }
        };

        FrameUniformAllocator* allocator_ = nullptr;
        std::unordered_map<const RTScene*, UniformBufferAllocation> frameUniforms_;
        std::unordered_map<ViewUniformKey, UniformBufferAllocation, ViewUniformKeyHash> viewUniforms_;
        std::unordered_map<const RTRenderItem*, UniformBufferAllocation> objectUniforms_;
    };
} // namespace ve
