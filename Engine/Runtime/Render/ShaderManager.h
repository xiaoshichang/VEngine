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
    struct ShaderID
    {
        std::string name;
        Int32 variant = 0;

        [[nodiscard]] bool operator==(const ShaderID& other) const noexcept;
    };

    struct ShaderIDHash
    {
        [[nodiscard]] SizeT operator()(const ShaderID& id) const noexcept;
    };

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

    class ShaderManager final : public NonMovable
    {
    public:
        ShaderManager() = default;
        ~ShaderManager() = default;

        [[nodiscard]] rhi::RhiShaderModule* GetShader(ShaderID id) noexcept;
        [[nodiscard]] const rhi::RhiShaderModule* GetShader(ShaderID id) const noexcept;
        [[nodiscard]] rhi::RhiShaderModule* GetOrCompileShader(rhi::RhiDevice& device, ShaderID id, const rhi::RhiShaderModuleDesc& desc);
        [[nodiscard]] rhi::RhiPipelineState* GetGraphicsPipeline(GraphicsPipelineID id) noexcept;
        [[nodiscard]] const rhi::RhiPipelineState* GetGraphicsPipeline(GraphicsPipelineID id) const noexcept;
        [[nodiscard]] rhi::RhiPipelineState* GetOrCreateGraphicsPipeline(rhi::RhiDevice& device,
                                                                         GraphicsPipelineID id,
                                                                         const rhi::RhiGraphicsPipelineDesc& desc);
        [[nodiscard]] rhi::RhiComputePipelineState* GetComputePipeline(ComputePipelineID id) noexcept;
        [[nodiscard]] rhi::RhiComputePipelineState*
        GetOrCreateComputePipeline(rhi::RhiDevice& device, ComputePipelineID id, const rhi::RhiComputePipelineDesc& desc);
        void Clear() noexcept;

    private:
        std::unordered_map<ShaderID, std::unique_ptr<rhi::RhiShaderModule>, ShaderIDHash> shaders_;
        std::unordered_map<GraphicsPipelineID, std::unique_ptr<rhi::RhiPipelineState>, GraphicsPipelineIDHash> graphicsPipelines_;
        std::unordered_map<ComputePipelineID, std::unique_ptr<rhi::RhiComputePipelineState>, ComputePipelineIDHash> computePipelines_;
    };
} // namespace ve
