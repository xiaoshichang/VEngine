#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h"

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

    private:
        friend class FrameGraph;

        explicit FrameGraphPassResources(const FrameGraph& frameGraph) noexcept;

        const FrameGraph& frameGraph_;
    };

    struct FrameGraphExecuteContext
    {
        const FrameRenderPipelineData& frameData;
        const RendererData& rendererData;
    };

    /// Owns one renderer invocation's logical resources and compiled pass order.
    class FrameGraph final : public NonCopyable
    {
    public:
        explicit FrameGraph(FrameGraphExecuteContext context);
        ~FrameGraph();

        template<typename PassData, typename SetupCallback, typename ExecuteCallback>
        void AddRasterPass(std::string name, SetupCallback&& setupCallback, ExecuteCallback&& executeCallback)
        {
            static_assert(std::is_default_constructible_v<PassData>, "Frame graph pass data must be default constructible.");

            auto passData = std::make_shared<PassData>();
            auto setup = std::make_shared<std::decay_t<SetupCallback>>(std::forward<SetupCallback>(setupCallback));
            auto execute = std::make_shared<std::decay_t<ExecuteCallback>>(std::forward<ExecuteCallback>(executeCallback));

            AddRasterPassInternal(std::move(name),
                                  [passData, setup](FrameGraphBuilder& builder) { (*setup)(builder, *passData); },
                                  [passData, execute](const FrameGraphPassResources& resources, RenderPassContext& context)
                                  {
                                      if constexpr (std::is_invocable_r_v<ErrorCode,
                                                                           std::decay_t<ExecuteCallback>&,
                                                                           const PassData&,
                                                                           const FrameGraphPassResources&,
                                                                           RenderPassContext&>)
                                      {
                                          return (*execute)(*passData, resources, context);
                                      }
                                      else
                                      {
                                          static_assert(std::is_invocable_r_v<ErrorCode,
                                                                              std::decay_t<ExecuteCallback>&,
                                                                              const PassData&,
                                                                              RenderPassContext&>,
                                                        "Frame graph execute callback has an unsupported signature.");
                                          return (*execute)(*passData, context);
                                      }
                                  });
        }

        [[nodiscard]] FrameGraphTextureHandle CreateTexture(std::string name, FrameGraphTextureDesc desc);
        [[nodiscard]] FrameGraphTextureHandle
        ImportTexture(std::string name, FrameGraphTextureDesc desc, ImportedFrameGraphTexture importedTexture);
        void Export(FrameGraphTextureHandle handle);

        [[nodiscard]] Error Compile();
        [[nodiscard]] ErrorCode Execute();

    private:
        friend class FrameGraphBuilder;
        friend class FrameGraphPassResources;

        using SetupFunction = std::function<void(FrameGraphBuilder&)>;
        using ExecuteFunction = std::function<ErrorCode(const FrameGraphPassResources&, RenderPassContext&)>;

        void AddRasterPassInternal(std::string name, SetupFunction setupFunction, ExecuteFunction executeFunction);
        [[nodiscard]] FrameGraphTextureHandle ReadTexture(UInt32 passIndex, FrameGraphTextureHandle handle, FrameGraphTextureAccess access);
        [[nodiscard]] FrameGraphTextureHandle WriteTexture(UInt32 passIndex, FrameGraphTextureHandle handle, FrameGraphTextureAccess access);
        void SetColorAttachment(UInt32 passIndex,
                                FrameGraphTextureHandle handle,
                                rhi::RhiLoadAction loadAction,
                                rhi::RhiStoreAction storeAction,
                                rhi::RhiColor clearColor);
        void SetDepthAttachment(UInt32 passIndex,
                                FrameGraphTextureHandle handle,
                                rhi::RhiLoadAction loadAction,
                                rhi::RhiStoreAction storeAction,
                                rhi::RhiDepthStencilClearValue clearValue,
                                bool readOnly);
        void SetRenderArea(UInt32 passIndex, const rhi::RhiRenderArea& renderArea) noexcept;
        void SetViewport(UInt32 passIndex, const rhi::RhiViewport& viewport) noexcept;
        void SetScissor(UInt32 passIndex, const rhi::RhiScissorRect& scissorRect) noexcept;
        void SetSideEffect(UInt32 passIndex) noexcept;
        [[nodiscard]] ResolvedFrameGraphTexture ResolveTexture(FrameGraphTextureHandle handle) const noexcept;

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace ve
