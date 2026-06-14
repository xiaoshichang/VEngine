#include "Engine/Runtime/Render/FrameRenderer.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Scene/SceneSystem.h"

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

    bool TestSceneMaintainsRTSceneThroughRenderCommands()
    {
        bool passed = true;

        ve::RenderSystem renderSystem;
        passed &= ExpectOk(renderSystem.Initialize(ve::RenderSystemInitParam{}),
                           "RenderSystem should initialize for RTScene command tests");
        ve::TimeSystem timeSystem;
        passed &= ExpectOk(timeSystem.Initialize(ve::TimeSystemInitParam{}),
                           "TimeSystem should initialize for RTScene command tests");
        ve::InputSystem inputSystem;
        passed &= ExpectOk(inputSystem.Initialize(ve::InputSystemInitParam{}),
                           "InputSystem should initialize for RTScene command tests");
        ve::SceneSystem sceneSystem;
        passed &= ExpectOk(sceneSystem.Initialize(ve::SceneSystemInitParam{}, timeSystem, inputSystem, renderSystem),
                           "SceneSystem should initialize with the RenderSystem");
        if (!passed)
        {
            sceneSystem.Shutdown();
            inputSystem.Shutdown();
            timeSystem.Shutdown();
            renderSystem.Shutdown();
            return false;
        }

        ve::Scene* scene = sceneSystem.GetScene();
        passed &= Expect(scene != nullptr, "SceneSystem should own an active Scene");
        if (scene == nullptr)
        {
            sceneSystem.Shutdown();
            inputSystem.Shutdown();
            timeSystem.Shutdown();
            renderSystem.Shutdown();
            return false;
        }
        scene->SetName("RTScene");

        ve::Result<ve::GameObject*> rootResult = scene->CreateRootGameObject("RootMesh");
        passed &= Expect(rootResult.IsOk(), "Scene should create a root GameObject");
        if (!rootResult.IsOk())
        {
            sceneSystem.Shutdown();
            inputSystem.Shutdown();
            timeSystem.Shutdown();
            renderSystem.Shutdown();
            return false;
        }

        ve::GameObject* root = rootResult.MoveValue();
        ve::Result<ve::MeshRenderComponent*> meshResult = root->AddComponent<ve::MeshRenderComponent>();
        passed &= Expect(meshResult.IsOk(), "GameObject should add MeshRenderComponent");
        if (!meshResult.IsOk())
        {
            return false;
        }

        ve::MeshRenderComponent* mesh = meshResult.MoveValue();
        passed &= Expect(mesh != nullptr, "GameObject should own MeshRenderComponent");

        if (mesh != nullptr)
        {
            mesh->SetMeshAssetPath("Assets/Meshes/Cube.veasset");
            mesh->SetMaterialAssetPath("Assets/Materials/Default.vematerial");
            mesh->SetBoundsCenter(ve::Vector3(1.0f, 2.0f, 3.0f));
            mesh->SetBoundsExtents(ve::Vector3(4.0f, 5.0f, 6.0f));
        }

        passed &= ExpectOk(renderSystem.Flush(), "RenderSystem should flush RTScene add/update commands");

        std::shared_ptr<ve::RTScene> rtScene = scene->GetRTScene();
        passed &= Expect(rtScene != nullptr, "Scene should own an RTScene");
        passed &= Expect(rtScene->GetRenderItemCount() == 1, "RTScene should contain one RTRenderItem");

        std::shared_ptr<ve::RTRenderItem> rtRenderItem = rtScene->GetRenderItem(0);
        passed &= Expect(rtRenderItem == mesh->GetRTRenderItem(), "MeshRenderComponent should reference its RTRenderItem");
        if (rtRenderItem != nullptr)
        {
            const ve::RTRenderItemDesc& desc = rtRenderItem->GetDesc();
            passed &= Expect(desc.meshAssetPath == "Assets/Meshes/Cube.veasset",
                             "RTRenderItem should carry the mesh asset path");
            passed &= Expect(desc.materialAssetPath == "Assets/Materials/Default.vematerial",
                             "RTRenderItem should carry the material asset path");
            passed &= Expect(desc.boundsCenter == ve::Vector3(1.0f, 2.0f, 3.0f),
                             "RTRenderItem should carry bounds center");
        }

        passed &= Expect(root->RemoveComponent<ve::MeshRenderComponent>(),
                         "GameObject should remove MeshRenderComponent");
        passed &= ExpectOk(renderSystem.Flush(), "RenderSystem should flush RTScene remove commands");
        passed &= Expect(rtScene->GetRenderItemCount() == 0, "RTScene should remove the RTRenderItem");

        sceneSystem.Shutdown();
        renderSystem.Shutdown();
        inputSystem.Shutdown();
        timeSystem.Shutdown();

        return passed;
    }
} // namespace

int main()
{
    if (TestFrameRendererBuildsAndExecutesPass() && TestSceneMaintainsRTSceneThroughRenderCommands())
    {
        std::cout << "VEngineRenderTests passed" << '\n';
        return 0;
    }

    return 1;
}
