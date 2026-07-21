#include "Engine/Runtime/Scene/SceneSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Physics/PhysicsSystem.h"
#include "Engine/Runtime/Platform/AutoreleasePool.h"
#include "Engine/Runtime/Render/RenderFramePipeline.h"
#include "Engine/Runtime/Render/RenderTexture.h"
#include "Engine/Runtime/Render/RenderViewState.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/MeshRenderComponent.h"
#include "Engine/Runtime/Scene/SceneSerialization.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Scripting/ScriptingSystem.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <exception>
#include <new>
#include <string_view>
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
        PhysicsSystem* physicsSystem = nullptr;

        // frame sync between main thread and render thread.
        ManualResetEvent startLoopEvent;
        MainThreadSceneThreadFrameEndSync* mainThreadSceneThreadFrameEndSync = nullptr;
        SceneThreadRenderThreadFrameEndSync* sceneThreadRenderThreadFrameEndSync = nullptr;
        SceneSystemEditorCallback editorCallback;
        std::function<void()> runtimeStartFrameCallback;
        std::function<void(const OSEvent& event)> runtimeOSEventCallback;
        std::shared_ptr<RTRenderTexture> playerSceneColorTexture;
        std::shared_ptr<RenderViewState> playerViewState;
        std::shared_ptr<RTCamera> activePlayerCamera;

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
            if (impl.scene != nullptr && impl.scene->GetExecutionMode() == SceneExecutionMode::Runtime)
            {
                impl.scene->Update(deltaSeconds);
                impl.scene->LateUpdate(deltaSeconds);
            }
        }

        void FixedUpdateScene(SceneSystemImpl& impl, const TimeSnapshot& timeSnapshot)
        {
            if (impl.scene == nullptr || impl.scene->GetExecutionMode() != SceneExecutionMode::Runtime || impl.physicsSystem == nullptr ||
                !impl.physicsSystem->IsInitialized())
            {
                return;
            }

            const Float32 fixedDeltaSeconds = timeSnapshot.fixedDeltaSeconds;
            for (UInt32 stepIndex = 0; stepIndex < timeSnapshot.fixedStepCount; ++stepIndex)
            {
                impl.scene->FixedUpdate(fixedDeltaSeconds);

                ErrorCode physicsResult = impl.physicsSystem->SyncSceneBeforeStep(*impl.scene);
                if (physicsResult == ErrorCode::None)
                {
                    physicsResult = impl.physicsSystem->StepSimulation(PhysicsStepDesc{fixedDeltaSeconds, 1});
                }
                if (physicsResult == ErrorCode::None)
                {
                    physicsResult = impl.physicsSystem->WriteBackSceneAfterStep(*impl.scene);
                }

                if (physicsResult != ErrorCode::None)
                {
                    VE_LOG_ERROR_CATEGORY("Physics", "Scene physics fixed step failed: {}", ToString(physicsResult));
                    return;
                }
            }
        }

        void ClearActiveScenePhysics(SceneSystemImpl& impl) noexcept
        {
            if (impl.scene != nullptr && impl.physicsSystem != nullptr && impl.physicsSystem->IsInitialized())
            {
                impl.physicsSystem->ClearSceneSyncState(*impl.scene);
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
            if (impl.inputSystem != nullptr)
            {
                impl.inputSystem->BeginFrame();
            }
            if (impl.editorCallback.onBeforeOSEvents != nullptr)
            {
                impl.editorCallback.onBeforeOSEvents();
            }
            ProcessOSEvents(impl);
            if (impl.runtimeStartFrameCallback != nullptr)
            {
                impl.runtimeStartFrameCallback();
            }
            if (impl.editorCallback.onStartFrame != nullptr)
            {
                impl.editorCallback.onStartFrame();
            }
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

            BaseRendererInitParam rendererInitParam = {};
            rendererInitParam.scene = impl.scene != nullptr ? impl.scene->GetRTScene() : nullptr;
            CameraComponent* camera = impl.scene != nullptr ? impl.scene->GetCamera() : nullptr;
            rendererInitParam.camera = camera != nullptr ? camera->GetRTCamera() : nullptr;
            VE_ASSERT_MESSAGE(impl.playerViewState != nullptr, "Player rendering requires a persistent render view state.");
            if (impl.activePlayerCamera.get() != rendererInitParam.camera.get())
            {
                impl.playerViewState->RequestCameraCut();
                impl.activePlayerCamera = rendererInitParam.camera;
            }
            rendererInitParam.viewState = impl.playerViewState->GetRTRenderViewState();
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
                PlatformAutoreleasePool autoreleasePool;

                try
                {
                    SceneThreadLoop_StartFrame(impl);

                    impl.timeSystem->Tick();
                    const TimeSnapshot timeSnapshot = impl.timeSystem->GetSnapshot();
                    FixedUpdateScene(impl, timeSnapshot);
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
            if (impl.scene != nullptr)
            {
                ClearActiveScenePhysics(impl);
                impl.scene->Clear();
            }
            impl.scene = nullptr;
            impl.renderSystem->Flush();

            impl.sceneThreadIdValue.store(0, std::memory_order_release);
            SetExpectedSceneThreadId(ThreadId{});
        }

        [[nodiscard]] Error
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
                        return Error(meshResource.GetError().GetCode(),
                                     "Failed to bind mesh asset '" + meshID.ToString() + "' on GameObject '" + gameObject.GetName() +
                                         "': " + meshResource.GetError().GetMessage());
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
                        return Error(materialResource.GetError().GetCode(),
                                     "Failed to bind material asset '" + materialID.ToString() + "' on GameObject '" + gameObject.GetName() +
                                         "': " + materialResource.GetError().GetMessage());
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
                return Error();
            }

            for (SizeT childIndex = 0; childIndex < transform->GetChildCount(); ++childIndex)
            {
                GameObject* child = transform->GetChildGameObject(childIndex);
                if (child == nullptr)
                {
                    continue;
                }

                Error result = BindGameObjectAssetRefs(*child, provider, resourceSystem, renderSystem);
                if (!result.IsOk())
                {
                    return result;
                }
            }

            return Error();
        }

        [[nodiscard]] Error BindSceneAssetRefs(Scene& scene, const IAssetRecordProvider& provider, ResourceSystem& resourceSystem, RenderSystem* renderSystem)
        {
            for (SizeT rootIndex = 0; rootIndex < scene.GetRootGameObjectCount(); ++rootIndex)
            {
                GameObject* root = scene.GetRootGameObject(rootIndex);
                if (root == nullptr)
                {
                    continue;
                }

                Error result = BindGameObjectAssetRefs(*root, provider, resourceSystem, renderSystem);
                if (!result.IsOk())
                {
                    return result;
                }
            }

            return Error();
        }

        [[nodiscard]] ErrorCode ValidateSceneLoadMode(SceneLoadMode mode)
        {
            if (mode == SceneLoadMode::Additive)
            {
                return ErrorCode::Unsupported;
            }

            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode ValidateSceneLoadRequest(const SceneLoadRequest& request)
        {
            const ErrorCode modeResult = ValidateSceneLoadMode(request.mode);
            if (modeResult != ErrorCode::None)
            {
                return modeResult;
            }

            if (request.provider == nullptr || request.resourceSystem == nullptr || request.scriptingSystem == nullptr)
            {
                return ErrorCode::InvalidArgument;
            }

            return ErrorCode::None;
        }

        [[nodiscard]] Result<AssetRef<SceneResource>> RequestSceneResource(const SceneLoadRequest& request)
        {
            return request.resourceSystem->Request<SceneResource>(request.scene, *request.provider);
        }

        [[nodiscard]] Result<std::unique_ptr<Scene>>
        CreateSceneFromText(SceneSystem& sceneSystem, SceneExecutionMode executionMode, std::string_view sceneText, ScriptingSystem& scriptingSystem)
        {
            auto scene = std::make_unique<Scene>(sceneSystem, executionMode);
            const ErrorCode deserializeResult = SceneSerialization::LoadFromString(*scene, sceneText, scriptingSystem);
            if (deserializeResult != ErrorCode::None)
            {
                return Result<std::unique_ptr<Scene>>::Failure(Error(deserializeResult, "Failed to deserialize scene resource."));
            }

            return Result<std::unique_ptr<Scene>>::Success(std::move(scene));
        }

        [[nodiscard]] Result<std::unique_ptr<Scene>> BuildSceneFromText(SceneSystem& sceneSystem,
                                                                        SceneExecutionMode executionMode,
                                                                        std::string_view sceneText,
                                                                        const IAssetRecordProvider& provider,
                                                                        ResourceSystem& resourceSystem,
                                                                        RenderSystem* renderSystem,
                                                                        ScriptingSystem& scriptingSystem)
        {
            Result<std::unique_ptr<Scene>> scene = CreateSceneFromText(sceneSystem, executionMode, sceneText, scriptingSystem);
            if (!scene)
            {
                return scene;
            }

            Error bindResult = BindSceneAssetRefs(*scene.GetValue(), provider, resourceSystem, renderSystem);
            if (!bindResult.IsOk())
            {
                return Result<std::unique_ptr<Scene>>::Failure(
                    Error(bindResult.GetCode(), "Failed to bind scene asset references: " + bindResult.GetMessage()));
            }

            return scene;
        }

        [[nodiscard]] Result<std::unique_ptr<Scene>> BuildSceneFromResource(SceneSystem& sceneSystem,
                                                                            SceneExecutionMode executionMode,
                                                                            const SceneResource& sceneResource,
                                                                            const IAssetRecordProvider& provider,
                                                                            ResourceSystem& resourceSystem,
                                                                            RenderSystem* renderSystem,
                                                                            ScriptingSystem& scriptingSystem)
        {
            return BuildSceneFromText(sceneSystem, executionMode, sceneResource.GetText(), provider, resourceSystem, renderSystem, scriptingSystem);
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

    ErrorCode SceneSystem::Initialize(
        const SceneSystemInitParam& initParam, TimeSystem& timeSystem, InputSystem& inputSystem, RenderSystem& renderSystem, PhysicsSystem& physicsSystem)
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

        if (!physicsSystem.IsInitialized())
        {
            return ErrorCode::InvalidState;
        }

        impl_->timeSystem = &timeSystem;
        impl_->inputSystem = &inputSystem;
        impl_->renderSystem = &renderSystem;
        impl_->physicsSystem = &physicsSystem;
        impl_->playerViewState = std::make_shared<RenderViewState>(RenderViewStateDesc{"PlayerView", 4096});
        impl_->activePlayerCamera.reset();
        impl_->stopRequested.store(false, std::memory_order_release);
        impl_->startLoopEvent.Reset();

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

        StopAndJoinSceneThread();
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

    void SceneSystem::StopAndJoinSceneThread() noexcept
    {
        impl_->stopRequested.store(true, std::memory_order_release);
        impl_->startLoopEvent.Set();
        if (impl_->mainThreadSceneThreadFrameEndSync != nullptr)
        {
            impl_->mainThreadSceneThreadFrameEndSync->UnblockAllWaiters();
        }

        if (impl_->sceneThreadRenderThreadFrameEndSync != nullptr)
        {
            impl_->sceneThreadRenderThreadFrameEndSync->UnblockAllWaiters();
        }

        if (impl_->thread.IsJoinable())
        {
            const bool joined = impl_->thread.Join();
            VE_ASSERT_MESSAGE(joined, "SceneSystem failed to join its Scene Thread during shutdown.");
        }

        impl_->initialized.store(false, std::memory_order_release);
        impl_->stopRequested.store(false, std::memory_order_release);
        impl_->timeSystem = nullptr;
        impl_->inputSystem = nullptr;
        VE_ASSERT(impl_->scene == nullptr);
        impl_->renderSystem = nullptr;
        impl_->physicsSystem = nullptr;
        impl_->runtimeStartFrameCallback = nullptr;
        impl_->runtimeOSEventCallback = nullptr;
        impl_->playerSceneColorTexture.reset();
        impl_->activePlayerCamera.reset();
        impl_->playerViewState.reset();
        impl_->osEventQueue.ClearForConsumer();
    }

    Result<std::unique_ptr<Scene>> SceneSystem::BuildSceneFromRequest(const SceneLoadRequest& request)
    {
        switch (request.source)
        {
        case SceneLoadSource::Asset:
        {
            Result<AssetRef<SceneResource>> sceneResource = RequestSceneResource(request);
            if (!sceneResource)
            {
                return Result<std::unique_ptr<Scene>>::Failure(sceneResource.GetError());
            }

            auto scene = BuildSceneFromResource(*this,
                                                request.executionMode,
                                                *sceneResource.GetValue().Get(),
                                                *request.provider,
                                                *request.resourceSystem,
                                                impl_->renderSystem,
                                                *request.scriptingSystem);
            return scene;
        }
        case SceneLoadSource::Text:
        {
            auto scene = BuildSceneFromText(
                *this, request.executionMode, request.sceneText, *request.provider, *request.resourceSystem, impl_->renderSystem, *request.scriptingSystem);
            return scene;
        }
        default:
            return Result<std::unique_ptr<Scene>>::Failure(Error(ErrorCode::InvalidArgument, "Unsupported scene load source."));
        }
    }

    Error SceneSystem::LoadScene(const SceneLoadRequest& request)
    {
        // 1. Validate the requested mode before touching resource state. SceneSystem currently owns a single active
        // Scene, so additive loading is rejected until multiple live Scene ownership is introduced.
        const ErrorCode validateResult = ValidateSceneLoadRequest(request);
        if (validateResult != ErrorCode::None)
        {
            return Error(validateResult, "Invalid scene load request.");
        }

        // 2. Build the new Scene from the request. This may involve deserializing a SceneResource or text, and binding
        // serialized AssetRefs. Keep the previous active Scene alive until the new Scene is fully constructed so load
        // failures do not cascade into a null active scene on the next render.
        Result<std::unique_ptr<Scene>> scene = BuildSceneFromRequest(request);
        if (!scene)
        {
            VE_LOG_ERROR_CATEGORY("Scene", "Failed to load scene: " + scene.GetError().GetMessage());
            return scene.GetError();
        }

        if (request.mode == SceneLoadMode::Single && impl_->scene != nullptr)
        {
            ClearActiveScenePhysics(*impl_);
            impl_->scene->Clear();
        }

        impl_->scene = scene.MoveValue();

        // 3. If the Scene was successfully built, set it as the active Scene and call its OnLoad() callback.
        impl_->scene->OnLoad();
        if (impl_->playerViewState != nullptr)
        {
            impl_->playerViewState->RequestCameraCut();
        }
        return Error();
    }

    void SceneSystem::UnloadActiveScene() noexcept
    {
        if (impl_->scene == nullptr)
        {
            return;
        }

        ClearActiveScenePhysics(*impl_);
        impl_->scene->Clear();
    }

    void SceneSystem::EnqueueOSEvent(const OSEvent& event)
    {
        const ErrorCode pushResult = impl_->osEventQueue.Push(event);
        VE_ASSERT_MESSAGE(pushResult == ErrorCode::None, "SceneSystem failed to enqueue OS event.");
    }

    void SceneSystem::EnqueueRenderCommand(RenderCommand command)
    {
        VE_ASSERT(impl_->renderSystem != nullptr && impl_->renderSystem->IsInitialized());
        impl_->renderSystem->EnqueueCommand(std::move(command));
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

    void SceneSystem::SetRuntimeStartFrameCallback(std::function<void()> callback) noexcept
    {
        impl_->runtimeStartFrameCallback = std::move(callback);
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
