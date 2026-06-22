#include "Engine/Runtime/Scene/SceneSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/RenderFramePipeline.h"
#include "Engine/Runtime/Render/RenderTexture.h"
#include "Engine/Runtime/Scene/MeshRenderComponent.h"
#include "Engine/Runtime/Scene/SceneSerialization.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <exception>
#include <new>
#include <utility>

namespace ve
{
    struct SceneSystemImpl
    {
        Thread thread;
        Atomic<UInt64> sceneThreadIdValue{0};
        std::unique_ptr<Scene> scene;
        OSEventQueue osEventQueue;
        TimeSystem* timeSystem = nullptr;
        InputSystem* inputSystem = nullptr;
        RenderSystem* renderSystem = nullptr;

        // frame sync between main thread and render thread.
        ManualResetEvent startLoopEvent;
        MainThreadSceneThreadFrameEndSync* mainThreadSceneThreadFrameEndSync = nullptr;
        SceneThreadRenderThreadFrameEndSync* sceneThreadRenderThreadFrameEndSync = nullptr;
        SceneSystemEditorCallback editorCallback;
        std::function<void(const OSEvent& event)> runtimeOSEventCallback;
        std::shared_ptr<RTRenderTexture> playerSceneColorTexture;

        AtomicBool initialized{false};
        AtomicBool stopRequested{false};
    };

    namespace
    {
        void ProcessOSEvents(SceneSystemImpl& impl)
        {
            const auto runtimeOnOSEvent = impl.runtimeOSEventCallback;
            const auto editorOnOSEvent = impl.editorCallback.onOSEvent;
            OSEvent event;
            while (impl.osEventQueue.TryPop(event))
            {
                if (event.type == OSEventType::FrameEndFenceSignal)
                {
                    if (impl.mainThreadSceneThreadFrameEndSync != nullptr)
                    {
                        impl.mainThreadSceneThreadFrameEndSync->NotifySceneThreadFrameEnd(event.fenceIndex);
                    }

                    continue;
                }

                if (runtimeOnOSEvent != nullptr)
                {
                    runtimeOnOSEvent(event);
                }

                bool shouldDispatchToInput = true;
                if (editorOnOSEvent != nullptr)
                {
                    shouldDispatchToInput = editorOnOSEvent(event);
                }
                if (shouldDispatchToInput)
                {
                    impl.inputSystem->ProcessOSEvent(event);
                }
            }
        }

        void UpdateScene(SceneSystemImpl& impl, Float32 deltaSeconds)
        {
            if (impl.scene != nullptr)
            {
                impl.scene->Update(deltaSeconds);
                impl.scene->LateUpdate(deltaSeconds);
            }
        }

        void BeforeRenderScene(SceneSystemImpl& impl)
        {
            if (impl.scene != nullptr)
            {
                impl.scene->BeforeRender();
            }
        }

        void SceneThreadLoop_StartFrame(SceneSystemImpl& impl)
        {
            if (impl.editorCallback.onStartFrame != nullptr)
            {
                impl.editorCallback.onStartFrame();
            }
            if (impl.inputSystem != nullptr)
            {
                impl.inputSystem->BeginFrame();
            }
            ProcessOSEvents(impl);
        }

        [[nodiscard]] std::shared_ptr<FrameRenderPipeline> CreatePlayerFramePipeline(SceneSystemImpl& impl)
        {
            VE_ASSERT_SCENE_THREAD();
            VE_ASSERT(impl.renderSystem != nullptr);

            if (impl.playerSceneColorTexture == nullptr)
            {
                RenderTextureDesc textureDesc = {};
                textureDesc.name = "PlayerSceneColor";
                impl.playerSceneColorTexture = std::make_shared<RTRenderTexture>(std::move(textureDesc));
            }

            ForwardRendererInitParam rendererInitParam = {};
            rendererInitParam.scene = impl.scene != nullptr ? impl.scene->GetRTScene() : nullptr;
            rendererInitParam.target.colorTexture = impl.playerSceneColorTexture;

            PlayerRenderFramePipelineInitParam pipelineInitParam = {};
            pipelineInitParam.sceneRenderer = std::move(rendererInitParam);
            pipelineInitParam.sceneColorTexture = impl.playerSceneColorTexture;
            return std::make_shared<PlayerRenderFramePipeline>(std::move(pipelineInitParam));
        }

        void SceneThreadLoop_Render_Editor(SceneSystemImpl& impl)
        {
            VE_ASSERT_SCENE_THREAD();
            VE_ASSERT(impl.renderSystem != nullptr);
            VE_ASSERT(impl.editorCallback.onRender != nullptr);

            std::shared_ptr<FrameRenderPipeline> framePipeline = impl.editorCallback.onRender();
            VE_ASSERT_MESSAGE(framePipeline != nullptr, "SceneThreadLoop_Render_Editor requires a frame pipeline.");

            impl.renderSystem->RenderFrame(std::move(framePipeline));
        }

        void SceneThreadLoop_Render_Player(SceneSystemImpl& impl)
        {
            VE_ASSERT_SCENE_THREAD();
            VE_ASSERT(impl.renderSystem != nullptr);

            impl.renderSystem->RenderFrame(CreatePlayerFramePipeline(impl));
        }

        void SceneThreadLoop_Render(SceneSystemImpl& impl)
        {
            if (impl.editorCallback.onRender != nullptr)
            {
                SceneThreadLoop_Render_Editor(impl);
            }
            else
            {
                SceneThreadLoop_Render_Player(impl);
            }
        }

        void SceneThreadLoop_EndFrame(SceneSystemImpl& impl)
        {
            impl.sceneThreadRenderThreadFrameEndSync->NotifySceneThreadFrameEndAndWait(
                impl.stopRequested, [&impl](UInt32 fenceIndex) { impl.renderSystem->SubmitFrameEndFenceSignal(fenceIndex); });
        }

        void SceneThreadLoop(SceneSystemImpl& impl)
        {
            const ThreadId sceneThreadId = GetCurrentThreadId();
            impl.sceneThreadIdValue.store(sceneThreadId.value, std::memory_order_release);
            SetExpectedSceneThreadId(sceneThreadId);
            impl.startLoopEvent.Wait();

            VE_ASSERT_MESSAGE(impl.timeSystem != nullptr, "impl.timeSystem should not be nullptr");
            VE_ASSERT_MESSAGE(impl.timeSystem->IsInitialized(), "impl.timeSystem should be initialized.");

            while (!impl.stopRequested.load(std::memory_order_acquire))
            {
                try
                {
                    SceneThreadLoop_StartFrame(impl);

                    impl.timeSystem->Tick();
                    const TimeSnapshot timeSnapshot = impl.timeSystem->GetSnapshot();
                    UpdateScene(impl, timeSnapshot.deltaSeconds);

                    BeforeRenderScene(impl);
                    SceneThreadLoop_Render(impl);
                    SceneThreadLoop_EndFrame(impl);
                }
                catch (...)
                {
                    VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped SceneSystem update.");
                }
            }

            impl.sceneThreadIdValue.store(0, std::memory_order_release);
            SetExpectedSceneThreadId(ThreadId{});
        }

        void StopAndJoinSceneThread(SceneSystemImpl& impl) noexcept
        {
            impl.stopRequested.store(true, std::memory_order_release);
            impl.startLoopEvent.Set();
            if (impl.mainThreadSceneThreadFrameEndSync != nullptr)
            {
                impl.mainThreadSceneThreadFrameEndSync->UnblockAllWaiters();
            }

            if (impl.sceneThreadRenderThreadFrameEndSync != nullptr)
            {
                impl.sceneThreadRenderThreadFrameEndSync->UnblockAllWaiters();
            }

            if (impl.thread.IsJoinable())
            {
                const bool joined = impl.thread.Join();
                VE_ASSERT_MESSAGE(joined, "SceneSystem failed to join its Scene Thread during shutdown.");
            }

            impl.initialized.store(false, std::memory_order_release);
            impl.stopRequested.store(false, std::memory_order_release);
            impl.timeSystem = nullptr;
            impl.inputSystem = nullptr;
            if (impl.scene != nullptr)
            {
                impl.scene->Clear();
                if (impl.renderSystem != nullptr)
                {
                    impl.renderSystem->Flush();
                }
                impl.scene->SetSceneSystem(nullptr);
            }
            impl.renderSystem = nullptr;
            impl.runtimeOSEventCallback = nullptr;
            impl.playerSceneColorTexture.reset();
            impl.osEventQueue.ClearForConsumer();
        }

        [[nodiscard]] ErrorCode
        BindGameObjectAssetRefs(GameObject& gameObject, const IAssetRecordProvider& provider, ResourceSystem& resourceSystem, RenderSystem* renderSystem)
        {
            if (MeshRenderComponent* mesh = gameObject.GetComponent<MeshRenderComponent>(); mesh != nullptr)
            {
                const AssetID meshID = mesh->GetMeshAssetID();
                if (!meshID.IsEmpty())
                {
                    Result<AssetRef<MeshResource>> meshResource = resourceSystem.Request<MeshResource>(meshID, provider);
                    if (!meshResource)
                    {
                        return meshResource.GetError().GetCode();
                    }

                    if (renderSystem != nullptr && renderSystem->IsInitialized())
                    {
                        resourceSystem.EnsureRenderResource(meshResource.GetValue(), *renderSystem);
                    }

                    mesh->SetMesh(meshResource.MoveValue());
                }

                const AssetID materialID = mesh->GetMaterialAssetID();
                if (!materialID.IsEmpty())
                {
                    Result<AssetRef<MaterialResource>> materialResource = resourceSystem.Request<MaterialResource>(materialID, provider);
                    if (!materialResource)
                    {
                        return materialResource.GetError().GetCode();
                    }

                    if (renderSystem != nullptr && renderSystem->IsInitialized())
                    {
                        resourceSystem.EnsureRenderResource(materialResource.GetValue(), *renderSystem);
                    }

                    mesh->SetMaterial(materialResource.MoveValue());
                }
            }

            TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
            if (transform == nullptr)
            {
                return ErrorCode::None;
            }

            for (SizeT childIndex = 0; childIndex < transform->GetChildCount(); ++childIndex)
            {
                GameObject* child = transform->GetChildGameObject(childIndex);
                if (child == nullptr)
                {
                    continue;
                }

                const ErrorCode result = BindGameObjectAssetRefs(*child, provider, resourceSystem, renderSystem);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }

            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode
        BindSceneAssetRefs(Scene& scene, const IAssetRecordProvider& provider, ResourceSystem& resourceSystem, RenderSystem* renderSystem)
        {
            for (SizeT rootIndex = 0; rootIndex < scene.GetRootGameObjectCount(); ++rootIndex)
            {
                GameObject* root = scene.GetRootGameObject(rootIndex);
                if (root == nullptr)
                {
                    continue;
                }

                const ErrorCode result = BindGameObjectAssetRefs(*root, provider, resourceSystem, renderSystem);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }

            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode ValidateSceneLoadDesc(const SceneLoadDesc& desc)
        {
            if (desc.mode == SceneLoadMode::Additive)
            {
                return ErrorCode::Unsupported;
            }

            return ErrorCode::None;
        }

        [[nodiscard]] Result<AssetRef<SceneResource>>
        RequestSceneResource(const SceneLoadDesc& desc, const IAssetRecordProvider& provider, ResourceSystem& resourceSystem)
        {
            return resourceSystem.Request<SceneResource>(desc.scene, provider);
        }

        [[nodiscard]] Result<std::unique_ptr<Scene>> CreateSceneFromResource(const SceneResource& sceneResource)
        {
            auto scene = std::make_unique<Scene>();
            const ErrorCode deserializeResult = SceneSerialization::LoadFromString(*scene, sceneResource.GetText());
            if (deserializeResult != ErrorCode::None)
            {
                return Result<std::unique_ptr<Scene>>::Failure(Error(deserializeResult, "Failed to deserialize scene resource."));
            }

            return Result<std::unique_ptr<Scene>>::Success(std::move(scene));
        }

        [[nodiscard]] Result<std::unique_ptr<Scene>> BuildSceneFromResource(const SceneResource& sceneResource,
                                                                            const IAssetRecordProvider& provider,
                                                                            ResourceSystem& resourceSystem,
                                                                            RenderSystem* renderSystem)
        {
            Result<std::unique_ptr<Scene>> scene = CreateSceneFromResource(sceneResource);
            if (!scene)
            {
                return scene;
            }

            const ErrorCode bindResult = BindSceneAssetRefs(*scene.GetValue(), provider, resourceSystem, renderSystem);
            if (bindResult != ErrorCode::None)
            {
                return Result<std::unique_ptr<Scene>>::Failure(Error(bindResult, "Failed to bind scene asset references."));
            }

            return scene;
        }

        [[nodiscard]] Scene* CommitLoadedScene(SceneSystemImpl& impl, SceneSystem& owner, SceneLoadMode mode, std::unique_ptr<Scene> scene)
        {
            if (mode == SceneLoadMode::Single && impl.scene != nullptr)
            {
                impl.scene->Clear();
                impl.scene->SetSceneSystem(nullptr);
            }

            Scene* loadedScene = scene.get();
            impl.scene = std::move(scene);
            impl.scene->SetSceneSystem(&owner);
            return loadedScene;
        }
    } // namespace

    SceneSystem::SceneSystem()
        : impl_(std::make_unique<SceneSystemImpl>())
    {
    }

    SceneSystem::~SceneSystem()
    {
        Shutdown();
    }

    ErrorCode SceneSystem::Initialize(const SceneSystemInitParam& initParam, TimeSystem& timeSystem, InputSystem& inputSystem, RenderSystem& renderSystem)
    {
        if (impl_->initialized.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        if (!timeSystem.IsInitialized())
        {
            return ErrorCode::InvalidState;
        }

        if (!inputSystem.IsInitialized())
        {
            return ErrorCode::InvalidState;
        }

        if (!renderSystem.IsInitialized())
        {
            return ErrorCode::InvalidState;
        }

        impl_->timeSystem = &timeSystem;
        impl_->inputSystem = &inputSystem;
        impl_->renderSystem = &renderSystem;
        impl_->stopRequested.store(false, std::memory_order_release);
        impl_->startLoopEvent.Reset();
        if (impl_->scene == nullptr)
        {
            impl_->scene = std::make_unique<Scene>();
        }
        impl_->scene->SetSceneSystem(this);

        if (impl_->mainThreadSceneThreadFrameEndSync != nullptr)
        {
            impl_->mainThreadSceneThreadFrameEndSync->Reset();
        }

        if (impl_->sceneThreadRenderThreadFrameEndSync != nullptr)
        {
            impl_->sceneThreadRenderThreadFrameEndSync->Reset();
        }

        ErrorCode startResult = impl_->thread.Start(ThreadDesc{initParam.threadName}, [this]() { SceneThreadLoop(*impl_); });
        if (startResult != ErrorCode::None)
        {
            throw;
        }

        impl_->initialized.store(true, std::memory_order_release);
        return ErrorCode::None;
    }

    void SceneSystem::Shutdown() noexcept
    {
        if (!impl_->initialized.load(std::memory_order_acquire))
        {
            return;
        }

        StopAndJoinSceneThread(*impl_);
    }

    bool SceneSystem::IsInitialized() const noexcept
    {
        return impl_->initialized.load(std::memory_order_acquire);
    }

    ThreadId SceneSystem::GetSceneThreadId() const noexcept
    {
        return ThreadId{impl_->sceneThreadIdValue.load(std::memory_order_acquire)};
    }

    Scene* SceneSystem::GetScene() noexcept
    {
        return impl_->scene.get();
    }

    const Scene* SceneSystem::GetScene() const noexcept
    {
        return impl_->scene.get();
    }

    Result<Scene*> SceneSystem::LoadScene(const SceneLoadDesc& desc, const IAssetRecordProvider& provider, ResourceSystem& resourceSystem)
    {
        // 1. Validate the requested mode before touching resource state. SceneSystem currently owns a single active
        // Scene, so additive loading is rejected until multiple live Scene ownership is introduced.
        const ErrorCode validateResult = ValidateSceneLoadDesc(desc);
        if (validateResult != ErrorCode::None)
        {
            return Result<Scene*>::Failure(Error(validateResult, "Unsupported scene load mode."));
        }

        // 2. Request the serialized SceneResource first. A failure here leaves the current active scene untouched.
        Result<AssetRef<SceneResource>> sceneResource = RequestSceneResource(desc, provider, resourceSystem);
        if (!sceneResource)
        {
            return Result<Scene*>::Failure(sceneResource.GetError());
        }

        // 3. Build a detached Scene and bind all component AssetRefs before committing it as the active scene. Render
        // resources are submitted here too, while the new scene is still detached, so scene deserialization or asset
        // binding failures leave the old scene untouched.
        Result<std::unique_ptr<Scene>> scene = BuildSceneFromResource(*sceneResource.GetValue().Get(), provider, resourceSystem, impl_->renderSystem);
        if (!scene)
        {
            return Result<Scene*>::Failure(scene.GetError());
        }

        // 4. Commit is the only phase that mutates the active scene pointer. SetSceneSystem() rebuilds render-thread
        // scene state after the new GameObject hierarchy and AssetRefs are complete.
        Scene* loadedScene = CommitLoadedScene(*impl_, *this, desc.mode, scene.MoveValue());
        return Result<Scene*>::Success(loadedScene);
    }

    void SceneSystem::UnloadActiveScene() noexcept
    {
        if (impl_->scene == nullptr)
        {
            return;
        }

        impl_->scene->Clear();
    }

    void SceneSystem::EnqueueOSEvent(const OSEvent& event)
    {
        const ErrorCode pushResult = impl_->osEventQueue.Push(event);
        VE_ASSERT_MESSAGE(pushResult == ErrorCode::None, "SceneSystem failed to enqueue OS event.");
    }

    void SceneSystem::EnqueueRenderCommand(RenderCommand command)
    {
        VE_ASSERT_MESSAGE(HasRenderSystem(), "SceneSystem::EnqueueRenderCommand requires an initialized RenderSystem.");

        impl_->renderSystem->EnqueueCommand(std::move(command));
    }

    bool SceneSystem::HasRenderSystem() const noexcept
    {
        return impl_->renderSystem != nullptr && impl_->renderSystem->IsInitialized();
    }

    void SceneSystem::NotifyMainThreadFrameEnd()
    {
        impl_->mainThreadSceneThreadFrameEndSync->NotifyMainThreadFrameEnd(
            [this](UInt32 fenceIndex)
            {
                const ErrorCode pushResult = impl_->osEventQueue.Push(OSEvent{
                    OSEventType::FrameEndFenceSignal,
                    0,
                    0,
                    fenceIndex,
                });
                VE_ASSERT_MESSAGE(pushResult == ErrorCode::None, "SceneSystem failed to enqueue frame-end fence event.");
            });
    }

    void SceneSystem::SetMainThreadSceneThreadFrameEndSync(MainThreadSceneThreadFrameEndSync* sync) noexcept
    {
        VE_ASSERT_MESSAGE(!impl_->initialized.load(std::memory_order_acquire), "SetMainThreadSceneThreadFrameEndSync requires SceneSystem to be stopped.");
        impl_->mainThreadSceneThreadFrameEndSync = sync;
    }

    void SceneSystem::SetSceneThreadRenderThreadFrameEndSync(SceneThreadRenderThreadFrameEndSync* sync) noexcept
    {
        VE_ASSERT_MESSAGE(!impl_->initialized.load(std::memory_order_acquire), "SetSceneThreadRenderThreadFrameEndSync requires SceneSystem to be stopped.");
        impl_->sceneThreadRenderThreadFrameEndSync = sync;
    }

    void SceneSystem::SetEditorCallback(SceneSystemEditorCallback callback) noexcept
    {
        impl_->editorCallback = std::move(callback);
    }

    void SceneSystem::SetRuntimeOSEventCallback(std::function<void(const OSEvent& event)> callback) noexcept
    {
        impl_->runtimeOSEventCallback = std::move(callback);
    }

    void SceneSystem::StartLoop() noexcept
    {
        impl_->startLoopEvent.Set();
    }

} // namespace ve
