#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include "Engine/Runtime/Core/Assert.h"

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
} // namespace ve
