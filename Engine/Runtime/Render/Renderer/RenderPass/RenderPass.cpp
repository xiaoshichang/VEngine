#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include "Engine/Runtime/Core/Assert.h"

#include <exception>

namespace ve
{
    namespace
    {
        [[nodiscard]] rhi::RhiDevice& ResolveDevice(const FrameRenderPipelineData& frameData) noexcept
        {
            VE_ASSERT(frameData.device != nullptr);
            return *frameData.device;
        }
    } // namespace

    RenderPassContext::RenderPassContext(RenderPassContextInitParam initParam) noexcept
        : frameData(initParam.frameData)
        , rendererData(initParam.rendererData)
        , executionInfo(initParam.executionInfo)
        , device(ResolveDevice(initParam.frameData))
        , commandList(initParam.frameData.GetCommandList())
    {
    }

    const RendererViewData& RenderPassContext::GetView(UInt32 viewIndex) const noexcept
    {
        if (viewIndex >= rendererData.views.size())
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Render pass requested a renderer view index outside the active view family.");
            std::terminate();
        }
        return rendererData.views[viewIndex];
    }

} // namespace ve
