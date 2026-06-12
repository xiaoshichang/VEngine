#include "Engine/Runtime/Render/FrameRenderer.h"

#include <iostream>
#include <memory>

namespace
{
    class FakeSwapchain final : public ve::rhi::RhiSwapchain
    {
    public:
        [[nodiscard]] ve::rhi::RhiExtent2D GetExtent() const noexcept override
        {
            return ve::rhi::RhiExtent2D{1280, 720};
        }

        [[nodiscard]] ve::rhi::RhiFormat GetColorFormat() const noexcept override
        {
            return ve::rhi::RhiFormat::Bgra8Unorm;
        }

        [[nodiscard]] bool Present() override
        {
            return true;
        }
    };

    class FakeCommandList final : public ve::rhi::RhiCommandList
    {
    public:
        [[nodiscard]] bool Begin() override
        {
            ++beginCount;
            return true;
        }

        [[nodiscard]] bool End() override
        {
            ++endCount;
            return true;
        }

        [[nodiscard]] bool BeginRenderPass(ve::rhi::RhiSwapchain& swapchain,
                                           const ve::rhi::RhiRenderPassDesc& desc) override
        {
            (void)swapchain;
            ++beginRenderPassCount;
            lastRenderPassDesc = desc;
            return true;
        }

        void EndRenderPass() override
        {
            ++endRenderPassCount;
        }

        void SetPipeline(const ve::rhi::RhiPipelineState& pipelineState) override
        {
            (void)pipelineState;
        }

        void SetViewport(const ve::rhi::RhiViewport& viewport) override
        {
            ++setViewportCount;
            lastViewport = viewport;
        }

        void SetScissor(const ve::rhi::RhiScissorRect& scissorRect) override
        {
            ++setScissorCount;
            lastScissor = scissorRect;
        }

        void SetVertexBuffer(uint32_t slot, const ve::rhi::RhiBuffer& buffer, uint32_t stride, uint64_t offset) override
        {
            (void)slot;
            (void)buffer;
            (void)stride;
            (void)offset;
        }

        void Draw(uint32_t vertexCount, uint32_t firstVertex) override
        {
            (void)vertexCount;
            (void)firstVertex;
        }

        int beginCount = 0;
        int endCount = 0;
        int beginRenderPassCount = 0;
        int endRenderPassCount = 0;
        int setViewportCount = 0;
        int setScissorCount = 0;
        ve::rhi::RhiRenderPassDesc lastRenderPassDesc = {};
        ve::rhi::RhiViewport lastViewport = {};
        ve::rhi::RhiScissorRect lastScissor = {};
    };

    class TestRenderPass final : public ve::RenderPass
    {
    public:
        [[nodiscard]] const char* GetName() const noexcept override
        {
            return "TestRenderPass";
        }

        void Setup(ve::RenderPassBuilder& builder) override
        {
            ++setupCount;
            builder.AddSwapchainColorAttachment(ve::rhi::RhiLoadAction::Clear,
                                                ve::rhi::RhiStoreAction::Store,
                                                ve::rhi::RhiColor{0.1f, 0.2f, 0.3f, 1.0f});
        }

        void Execute(ve::RenderPassContext& context) override
        {
            ++executeCount;
            const ve::RenderFrameContext& frameContext = context.GetFrameContext();
            sawExpectedExtent = frameContext.mainSurfaceExtent.width == 1280 &&
                                frameContext.mainSurfaceExtent.height == 720;
        }

        int setupCount = 0;
        int executeCount = 0;
        bool sawExpectedExtent = false;
    };

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool ExpectOk(ve::ErrorCode result, const char* message)
    {
        if (result == ve::ErrorCode::None)
        {
            return true;
        }

        std::cerr << "FAILED: " << message << ": " << ve::ToString(result) << '\n';
        return false;
    }

    bool TestFrameRendererBuildsAndExecutesPass()
    {
        bool passed = true;

        FakeSwapchain swapchain;
        FakeCommandList commandList;
        auto pass = std::make_unique<TestRenderPass>();
        TestRenderPass* passPtr = pass.get();

        ve::FrameRenderer renderer;
        renderer.AddPass(std::move(pass));

        passed &= ExpectOk(renderer.BeginFrame(commandList, swapchain), "FrameRenderer should begin a frame");
        passed &= Expect(renderer.IsFrameActive(), "FrameRenderer should report an active frame after BeginFrame");
        passed &= ExpectOk(renderer.ExecutePassesInOrder(), "FrameRenderer should execute active passes");
        renderer.EndFrame();

        passed &= Expect(!renderer.IsFrameActive(), "FrameRenderer should report inactive after EndFrame");
        passed &= Expect(passPtr->setupCount == 1, "RenderPass::Setup should run once per frame");
        passed &= Expect(passPtr->executeCount == 1, "RenderPass::Execute should run once");
        passed &= Expect(passPtr->sawExpectedExtent, "RenderPassContext should expose the frame context");
        passed &= Expect(commandList.beginCount == 1, "RHI command list should begin once");
        passed &= Expect(commandList.endCount == 1, "RHI command list should end once");
        passed &= Expect(commandList.beginRenderPassCount == 1, "RHI render pass should begin once");
        passed &= Expect(commandList.endRenderPassCount == 1, "RHI render pass should end once");
        passed &= Expect(commandList.setViewportCount == 1, "FrameRenderer should set pass viewport");
        passed &= Expect(commandList.setScissorCount == 1, "FrameRenderer should set pass scissor");
        passed &= Expect(commandList.lastRenderPassDesc.colorAttachmentCount == 1,
                         "RenderPassBuilder should emit one color attachment");
        passed &= Expect(commandList.lastRenderPassDesc.colorAttachments[0].loadAction ==
                             ve::rhi::RhiLoadAction::Clear,
                         "Color attachment should carry the declared load action");
        passed &= Expect(commandList.lastViewport.width == 1280.0f && commandList.lastViewport.height == 720.0f,
                         "Default viewport should match the main surface extent");
        passed &= Expect(commandList.lastScissor.width == 1280 && commandList.lastScissor.height == 720,
                         "Default scissor should match the main surface extent");

        return passed;
    }
} // namespace

int main()
{
    if (TestFrameRendererBuildsAndExecutesPass())
    {
        std::cout << "VEngineRenderTests passed" << '\n';
        return 0;
    }

    return 1;
}
