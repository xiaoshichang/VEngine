#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace ve
{
    class FrameGraphBuilder;
    class FrameGraph;

    /// Resolves logical handles only while one compiled pass is executing.
    class FrameGraphPassResources final
    {
    public:
        [[nodiscard]] ResolvedFrameGraphTexture GetTexture(FrameGraphTextureHandle handle) const noexcept;
        [[nodiscard]] ResolvedFrameGraphBuffer GetBuffer(FrameGraphBufferHandle handle) const noexcept;

    private:
        friend class FrameGraph;

        FrameGraphPassResources(const FrameGraph& frameGraph, UInt32 passIndex) noexcept;

        const FrameGraph& frameGraph_;
        UInt32 passIndex_ = 0;
    };

    struct FrameGraphExecuteContext
    {
        const FrameRenderPipelineData& frameData;
        const RendererData& rendererData;
    };

    /// Owns one renderer invocation and enforces its Setup -> Compile -> Execute lifecycle.
    class FrameGraph final : public NonCopyable
    {
    public:
        using GraphSetupFunction = std::function<ErrorCode(FrameGraph&)>;

        explicit FrameGraph(FrameGraphExecuteContext context);
        ~FrameGraph();

        /// Setup phase: imports/creates resources, registers passes, declares accesses, and exports final resources.
        [[nodiscard]] ErrorCode Setup(GraphSetupFunction setupFunction);

        template<typename PassData, typename SetupCallback, typename ExecuteCallback>
        void AddRasterPass(std::string name, SetupCallback&& setupCallback, ExecuteCallback&& executeCallback)
        {
            static_assert(std::is_default_constructible_v<PassData>, "Frame graph pass data must be default constructible.");

            auto passData = std::make_shared<PassData>();
            auto setup = std::make_shared<std::decay_t<SetupCallback>>(std::forward<SetupCallback>(setupCallback));
            auto execute = std::make_shared<std::decay_t<ExecuteCallback>>(std::forward<ExecuteCallback>(executeCallback));

            AddRasterPassInternal(
                std::move(name),
                [passData, setup](FrameGraphBuilder& builder) { (*setup)(builder, *passData); },
                [passData, execute](const FrameGraphPassResources& resources, RenderPassContext& context)
                {
                    if constexpr (
                        std::is_invocable_r_v<ErrorCode, std::decay_t<ExecuteCallback>&, const PassData&, const FrameGraphPassResources&, RenderPassContext&>)
                    {
                        return (*execute)(*passData, resources, context);
                    }
                    else
                    {
                        static_cast<void>(resources);
                        static_assert(std::is_invocable_r_v<ErrorCode, std::decay_t<ExecuteCallback>&, const PassData&, RenderPassContext&>,
                                      "Frame graph execute callback has an unsupported signature.");
                        return (*execute)(*passData, context);
                    }
                });
        }

        template<typename PassData, typename SetupCallback, typename ExecuteCallback>
        void AddComputePass(std::string name, SetupCallback&& setupCallback, ExecuteCallback&& executeCallback)
        {
            static_assert(std::is_default_constructible_v<PassData>, "Frame graph pass data must be default constructible.");

            auto passData = std::make_shared<PassData>();
            auto setup = std::make_shared<std::decay_t<SetupCallback>>(std::forward<SetupCallback>(setupCallback));
            auto execute = std::make_shared<std::decay_t<ExecuteCallback>>(std::forward<ExecuteCallback>(executeCallback));

            AddComputePassInternal(
                std::move(name),
                [passData, setup](FrameGraphBuilder& builder) { (*setup)(builder, *passData); },
                [passData, execute](const FrameGraphPassResources& resources, RenderPassContext& context)
                {
                    if constexpr (
                        std::is_invocable_r_v<ErrorCode, std::decay_t<ExecuteCallback>&, const PassData&, const FrameGraphPassResources&, RenderPassContext&>)
                    {
                        return (*execute)(*passData, resources, context);
                    }
                    else
                    {
                        static_cast<void>(resources);
                        static_assert(std::is_invocable_r_v<ErrorCode, std::decay_t<ExecuteCallback>&, const PassData&, RenderPassContext&>,
                                      "Frame graph execute callback has an unsupported signature.");
                        return (*execute)(*passData, context);
                    }
                });
        }

        [[nodiscard]] FrameGraphTextureHandle CreateTexture(std::string name, FrameGraphTextureDesc desc);
        [[nodiscard]] FrameGraphTextureHandle ImportTexture(std::string name, FrameGraphTextureDesc desc, ImportedFrameGraphTexture importedTexture);
        [[nodiscard]] FrameGraphBufferHandle ImportBuffer(std::string name, ImportedFrameGraphBuffer importedBuffer);
        void Export(FrameGraphTextureHandle handle);
        void Export(FrameGraphBufferHandle handle);

        /// Compile phase: validates declarations, builds dependencies, culls/sorts passes, and analyzes transient lifetimes.
        [[nodiscard]] Error Compile();

        /// Execute phase: resolves physical resources and records each compiled pass into the current command list.
        [[nodiscard]] ErrorCode Execute();

        [[nodiscard]] const RendererData& GetRendererData() const noexcept;

    private:
        friend class FrameGraphBuilder;
        friend class FrameGraphPassResources;

        using PassSetupFunction = std::function<void(FrameGraphBuilder&)>;
        using ExecuteFunction = std::function<ErrorCode(const FrameGraphPassResources&, RenderPassContext&)>;

        void AddRasterPassInternal(std::string name, PassSetupFunction setupFunction, ExecuteFunction executeFunction);
        void AddComputePassInternal(std::string name, PassSetupFunction setupFunction, ExecuteFunction executeFunction);
        [[nodiscard]] FrameGraphTextureHandle ReadTexture(UInt32 passIndex, FrameGraphTextureHandle handle, FrameGraphTextureAccess access);
        [[nodiscard]] FrameGraphTextureHandle WriteTexture(UInt32 passIndex, FrameGraphTextureHandle handle, FrameGraphTextureAccess access);
        [[nodiscard]] FrameGraphBufferHandle ReadBuffer(UInt32 passIndex, FrameGraphBufferHandle handle, FrameGraphBufferAccess access);
        [[nodiscard]] FrameGraphBufferHandle WriteBuffer(UInt32 passIndex, FrameGraphBufferHandle handle, FrameGraphBufferAccess access);
        [[nodiscard]] FrameGraphTextureHandle
        WriteColorAttachment(UInt32 passIndex, FrameGraphTextureHandle handle, rhi::RhiLoadAction loadAction, rhi::RhiColor clearColor);
        [[nodiscard]] FrameGraphTextureHandle
        WriteDepthAttachment(UInt32 passIndex, FrameGraphTextureHandle handle, rhi::RhiLoadAction loadAction, Float32 clearDepth);
        [[nodiscard]] FrameGraphTextureHandle ReadDepthAttachment(UInt32 passIndex, FrameGraphTextureHandle handle);
        void SetRenderArea(UInt32 passIndex, const rhi::RhiRenderArea& renderArea) noexcept;
        void SetViewport(UInt32 passIndex, const rhi::RhiViewport& viewport) noexcept;
        void SetScissor(UInt32 passIndex, const rhi::RhiScissorRect& scissorRect) noexcept;
        void SetSideEffect(UInt32 passIndex) noexcept;
        [[nodiscard]] ResolvedFrameGraphTexture ResolveTexture(FrameGraphTextureHandle handle) const noexcept;
        [[nodiscard]] ResolvedFrameGraphTexture ResolvePassTexture(UInt32 passIndex, FrameGraphTextureHandle handle) const noexcept;
        [[nodiscard]] ResolvedFrameGraphBuffer ResolveBuffer(FrameGraphBufferHandle handle) const noexcept;
        [[nodiscard]] ResolvedFrameGraphBuffer ResolvePassBuffer(UInt32 passIndex, FrameGraphBufferHandle handle) const noexcept;

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace ve
