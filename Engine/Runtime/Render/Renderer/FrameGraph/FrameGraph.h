#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ve
{
    class FrameGraphBuilder;
    class FrameGraph;
    struct FrameGraphDebugFrameCapture;

    template<typename Callback>
    concept FrameGraphSetupCallback = requires(std::decay_t<Callback>& callback, FrameGraph& frameGraph) {
        { callback(frameGraph) } -> std::same_as<void>;
    };

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

    enum class FrameGraphPassType
    {
        Raster,
        Compute,
    };

    struct FrameGraphTextureAccessDiagnostics
    {
        FrameGraphTextureHandle input;
        FrameGraphTextureHandle output;
        FrameGraphTextureAccess access = FrameGraphTextureAccess::ShaderRead;
        bool write = false;

        [[nodiscard]] bool operator==(const FrameGraphTextureAccessDiagnostics&) const noexcept = default;
    };

    struct FrameGraphBufferAccessDiagnostics
    {
        FrameGraphBufferHandle input;
        FrameGraphBufferHandle output;
        FrameGraphBufferAccess access = FrameGraphBufferAccess::ShaderRead;
        bool write = false;

        [[nodiscard]] bool operator==(const FrameGraphBufferAccessDiagnostics&) const noexcept = default;
    };

    /// Immutable registration-time state for renderer diagnostics and graph inspection.
    struct FrameGraphPassDiagnostics
    {
        std::string name;
        FrameGraphPassType type = FrameGraphPassType::Raster;
        std::vector<FrameGraphBufferHandle> bufferUavBarriersBeforeExecute;
        std::vector<FrameGraphTextureHandle> textureUavBarriersBeforeExecute;
        std::optional<rhi::RhiLoadAction> depthAttachmentLoadAction;
        std::vector<FrameGraphTextureAccessDiagnostics> textureAccesses;
        std::vector<FrameGraphBufferAccessDiagnostics> bufferAccesses;
        std::optional<UInt32> compiledIndex;
        bool internal = false;
    };

    template<typename PassData, typename Callback>
    concept FrameGraphPassSetupCallback = requires(std::decay_t<Callback>& callback, FrameGraphBuilder& builder, PassData& passData) {
        { callback(builder, passData) } -> std::same_as<void>;
    };

    template<typename PassData, typename Callback>
    concept FrameGraphPassExecuteWithResourcesCallback =
        requires(std::decay_t<Callback>& callback, const PassData& passData, const FrameGraphPassResources& resources, RenderPassContext& context) {
            { callback(passData, resources, context) } -> std::same_as<void>;
        };

    template<typename PassData, typename Callback>
    concept FrameGraphPassExecuteWithoutResourcesCallback = requires(std::decay_t<Callback>& callback, const PassData& passData, RenderPassContext& context) {
        { callback(passData, context) } -> std::same_as<void>;
    };

    template<typename PassData, typename Callback>
    concept FrameGraphPassExecuteCallback =
        FrameGraphPassExecuteWithResourcesCallback<PassData, Callback> || FrameGraphPassExecuteWithoutResourcesCallback<PassData, Callback>;

    template<typename>
    inline constexpr bool FrameGraphAlwaysFalse = false;

    /// Owns one renderer invocation and enforces its Setup -> Compile -> Execute lifecycle.
    class FrameGraph final : public NonCopyable
    {
    public:
        using GraphSetupFunction = std::function<void(FrameGraph&)>;

        explicit FrameGraph(FrameGraphExecuteContext context);
        ~FrameGraph();

        /// Setup phase: imports/creates resources, registers passes, declares accesses, and exports final resources.
        template<FrameGraphSetupCallback SetupCallback>
        void Setup(SetupCallback&& setupCallback)
        {
            SetupInternal(GraphSetupFunction(std::forward<SetupCallback>(setupCallback)));
        }

        template<typename PassData, typename SetupCallback, typename ExecuteCallback>
            requires FrameGraphPassSetupCallback<PassData, SetupCallback> && FrameGraphPassExecuteCallback<PassData, ExecuteCallback>
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
                    if constexpr (FrameGraphPassExecuteWithResourcesCallback<PassData, ExecuteCallback>)
                    {
                        (*execute)(*passData, resources, context);
                    }
                    else if constexpr (FrameGraphPassExecuteWithoutResourcesCallback<PassData, ExecuteCallback>)
                    {
                        static_cast<void>(resources);
                        (*execute)(*passData, context);
                    }
                    else
                    {
                        static_assert(FrameGraphAlwaysFalse<ExecuteCallback>, "Frame graph execute callback has an unsupported signature.");
                    }
                });
        }

        template<typename PassData, typename SetupCallback, typename ExecuteCallback>
            requires FrameGraphPassSetupCallback<PassData, SetupCallback> && FrameGraphPassExecuteCallback<PassData, ExecuteCallback>
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
                    if constexpr (FrameGraphPassExecuteWithResourcesCallback<PassData, ExecuteCallback>)
                    {
                        (*execute)(*passData, resources, context);
                    }
                    else if constexpr (FrameGraphPassExecuteWithoutResourcesCallback<PassData, ExecuteCallback>)
                    {
                        static_cast<void>(resources);
                        (*execute)(*passData, context);
                    }
                    else
                    {
                        static_assert(FrameGraphAlwaysFalse<ExecuteCallback>, "Frame graph execute callback has an unsupported signature.");
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

        /// Compiles and snapshots the original declarations, then appends best-effort internal preview passes.
        [[nodiscard]] Error PrepareDebugCapture(FrameGraphDebugFrameCapture& capture);

        /// Execute phase: resolves physical resources and records each compiled pass into the current command list.
        [[nodiscard]] ErrorCode Execute();

        [[nodiscard]] const RendererData& GetRendererData() const noexcept;
        [[nodiscard]] std::vector<FrameGraphPassDiagnostics> GetPassDiagnostics() const;
        /// Returns the pass names that completed during the most recent Execute call, in actual execution order.
        [[nodiscard]] std::vector<std::string> GetLastExecutionPassNames() const;

    private:
        friend class FrameGraphBuilder;
        friend class FrameGraphPassResources;

        using PassSetupFunction = std::function<void(FrameGraphBuilder&)>;
        using ExecuteFunction = std::function<void(const FrameGraphPassResources&, RenderPassContext&)>;

        void SetupInternal(GraphSetupFunction setupFunction);
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
        void AddUavBarrierBeforeExecute(UInt32 passIndex, FrameGraphBufferHandle handle) noexcept;
        void AddUavBarrierBeforeExecute(UInt32 passIndex, FrameGraphTextureHandle handle) noexcept;
        void SetSideEffect(UInt32 passIndex) noexcept;
        [[nodiscard]] Error CompileInternal();
        [[nodiscard]] ResolvedFrameGraphTexture ResolveTexture(FrameGraphTextureHandle handle) const noexcept;
        [[nodiscard]] ResolvedFrameGraphTexture ResolvePassTexture(UInt32 passIndex, FrameGraphTextureHandle handle) const noexcept;
        [[nodiscard]] ResolvedFrameGraphBuffer ResolveBuffer(FrameGraphBufferHandle handle) const noexcept;
        [[nodiscard]] ResolvedFrameGraphBuffer ResolvePassBuffer(UInt32 passIndex, FrameGraphBufferHandle handle) const noexcept;

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace ve
