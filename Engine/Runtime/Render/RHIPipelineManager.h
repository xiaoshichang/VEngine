#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace ve
{
    struct GraphicsPipelineID
    {
        std::string name;
        Int32 variant = 0;

        [[nodiscard]] bool operator==(const GraphicsPipelineID& other) const noexcept;
    };

    struct GraphicsPipelineIDHash
    {
        [[nodiscard]] SizeT operator()(const GraphicsPipelineID& id) const noexcept;
    };

    using ComputePipelineID = GraphicsPipelineID;
    using ComputePipelineIDHash = GraphicsPipelineIDHash;

    class RHIPipelineManager final : public NonMovable
    {
    public:
        RHIPipelineManager() = default;
        ~RHIPipelineManager() = default;

        [[nodiscard]] rhi::RhiGraphicsPipelineState* GetGraphicsPipeline(GraphicsPipelineID id) noexcept;
        [[nodiscard]] const rhi::RhiGraphicsPipelineState* GetGraphicsPipeline(GraphicsPipelineID id) const noexcept;
        [[nodiscard]] rhi::RhiGraphicsPipelineState*
        TryGetOrCreateGraphicsPipeline(rhi::RhiDevice& device, GraphicsPipelineID id, const rhi::RhiGraphicsPipelineDesc& desc);
        [[nodiscard]] rhi::RhiGraphicsPipelineState*
        GetOrCreateGraphicsPipeline(rhi::RhiDevice& device, GraphicsPipelineID id, const rhi::RhiGraphicsPipelineDesc& desc);
        [[nodiscard]] rhi::RhiComputePipelineState* GetComputePipeline(ComputePipelineID id) noexcept;
        [[nodiscard]] const rhi::RhiComputePipelineState* GetComputePipeline(ComputePipelineID id) const noexcept;
        [[nodiscard]] rhi::RhiComputePipelineState*
        GetOrCreateComputePipeline(rhi::RhiDevice& device, ComputePipelineID id, const rhi::RhiComputePipelineDesc& desc);
        void Clear() noexcept;

    private:
        std::unordered_map<GraphicsPipelineID, std::unique_ptr<rhi::RhiGraphicsPipelineState>, GraphicsPipelineIDHash> graphicsPipelines_;
        std::unordered_map<ComputePipelineID, std::unique_ptr<rhi::RhiComputePipelineState>, ComputePipelineIDHash> computePipelines_;
    };
} // namespace ve
