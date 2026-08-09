#include "Engine/Runtime/Render/RHIPipelineManager.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <functional>
#include <string>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] const char* ToString(rhi::RhiBackend backend) noexcept
        {
            switch (backend)
            {
            case rhi::RhiBackend::D3D11:
                return "D3D11";
            case rhi::RhiBackend::D3D12:
                return "D3D12";
            case rhi::RhiBackend::Metal:
                return "Metal";
            }

            return "Unknown";
        }

        [[nodiscard]] std::string
        BuildGraphicsPipelineCreateFailureMessage(const rhi::RhiDevice& device, const GraphicsPipelineID& id, const rhi::RhiGraphicsPipelineDesc& desc)
        {
            std::string message = "RHIPipelineManager failed to create graphics pipeline.";
            message += " name='";
            message += id.name.empty() ? "<empty>" : id.name;
            message += "'";
            message += " variant=" + std::to_string(id.variant);
            message += " debugName='";
            message += desc.debugName != nullptr ? desc.debugName : "<null>";
            message += "'";
            message += " backend=";
            message += ToString(device.GetBackend());

            const char* backendError = device.GetLastErrorMessage();
            if (backendError != nullptr && backendError[0] != '\0')
            {
                message += " backendError='";
                message += backendError;
                message += "'";
            }

            return message;
        }
    } // namespace

    bool GraphicsPipelineID::operator==(const GraphicsPipelineID& other) const noexcept
    {
        return variant == other.variant && name == other.name;
    }

    SizeT GraphicsPipelineIDHash::operator()(const GraphicsPipelineID& id) const noexcept
    {
        const SizeT nameHash = std::hash<std::string>{}(id.name);
        const SizeT variantHash = std::hash<Int32>{}(id.variant);
        return nameHash ^ (variantHash + 0x9E3779B97F4A7C15ull + (nameHash << 6) + (nameHash >> 2));
    }

    rhi::RhiGraphicsPipelineState* RHIPipelineManager::GetGraphicsPipeline(GraphicsPipelineID id) noexcept
    {
        const auto existing = graphicsPipelines_.find(id);
        return existing != graphicsPipelines_.end() ? existing->second.get() : nullptr;
    }

    const rhi::RhiGraphicsPipelineState* RHIPipelineManager::GetGraphicsPipeline(GraphicsPipelineID id) const noexcept
    {
        const auto existing = graphicsPipelines_.find(id);
        return existing != graphicsPipelines_.end() ? existing->second.get() : nullptr;
    }

    rhi::RhiGraphicsPipelineState*
    RHIPipelineManager::TryGetOrCreateGraphicsPipeline(rhi::RhiDevice& device, GraphicsPipelineID id, const rhi::RhiGraphicsPipelineDesc& desc)
    {
        VE_ASSERT_RENDER_THREAD();
        if (rhi::RhiGraphicsPipelineState* pipeline = GetGraphicsPipeline(id); pipeline != nullptr)
        {
            return pipeline;
        }

        std::unique_ptr<rhi::RhiGraphicsPipelineState> pipeline = device.CreateGraphicsPipeline(desc);
        if (pipeline == nullptr)
        {
            const std::string message = BuildGraphicsPipelineCreateFailureMessage(device, id, desc);
            VE_LOG_ERROR("{}", message);
            return nullptr;
        }

        rhi::RhiGraphicsPipelineState* pipelinePtr = pipeline.get();
        graphicsPipelines_.emplace(std::move(id), std::move(pipeline));
        return pipelinePtr;
    }

    rhi::RhiGraphicsPipelineState*
    RHIPipelineManager::GetOrCreateGraphicsPipeline(rhi::RhiDevice& device, GraphicsPipelineID id, const rhi::RhiGraphicsPipelineDesc& desc)
    {
        rhi::RhiGraphicsPipelineState* pipeline = TryGetOrCreateGraphicsPipeline(device, id, desc);
        if (pipeline == nullptr)
        {
            const std::string message = BuildGraphicsPipelineCreateFailureMessage(device, id, desc);
            VE_ASSERT_MESSAGE(pipeline != nullptr, message.c_str());
        }
        return pipeline;
    }

    rhi::RhiComputePipelineState* RHIPipelineManager::GetComputePipeline(ComputePipelineID id) noexcept
    {
        const auto existing = computePipelines_.find(id);
        return existing != computePipelines_.end() ? existing->second.get() : nullptr;
    }

    const rhi::RhiComputePipelineState* RHIPipelineManager::GetComputePipeline(ComputePipelineID id) const noexcept
    {
        const auto existing = computePipelines_.find(id);
        return existing != computePipelines_.end() ? existing->second.get() : nullptr;
    }

    rhi::RhiComputePipelineState*
    RHIPipelineManager::GetOrCreateComputePipeline(rhi::RhiDevice& device, ComputePipelineID id, const rhi::RhiComputePipelineDesc& desc)
    {
        VE_ASSERT_RENDER_THREAD();
        if (rhi::RhiComputePipelineState* pipeline = GetComputePipeline(id); pipeline != nullptr)
        {
            return pipeline;
        }

        std::unique_ptr<rhi::RhiComputePipelineState> pipeline = device.CreateComputePipeline(desc);
        if (pipeline == nullptr)
        {
            VE_ASSERT_MESSAGE(pipeline != nullptr, device.GetLastErrorMessage());
            return nullptr;
        }

        rhi::RhiComputePipelineState* pipelinePtr = pipeline.get();
        computePipelines_.emplace(std::move(id), std::move(pipeline));
        return pipelinePtr;
    }

    void RHIPipelineManager::Clear() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        computePipelines_.clear();
        graphicsPipelines_.clear();
    }
} // namespace ve
