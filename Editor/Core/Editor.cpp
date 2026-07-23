#include "Editor/Core/Editor.h"

#include "Editor/Core/EditorProject.h"
#include "Editor/Core/EditorProjectEditingView.h"
#include "Editor/Core/EditorProjectSelectionView.h"
#include "Editor/Core/EditorRenderBackend.h"
#include "Editor/Core/EditorToolchain.h"
#include "Editor/RenderPass/EditorGizmoRenderPass.h"
#include "Editor/RenderPass/SceneGridRenderPass.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/MeshRenderComponent.h"
#include "Engine/Runtime/Scene/SceneSerialization.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <imgui_internal.h>

#if VE_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef GetMessage
#undef GetMessage
#endif
#endif

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>

namespace ve::editor
{
#if VE_PLATFORM_MACOS
    void ApplyMacMainWindowTitle(void* nativeWindowHandle, const std::string& title);
#endif

    struct EditorDrawListSnapshotEntry
    {
        ImDrawList* source = nullptr;
        ImDrawList* snapshot = nullptr;
        double lastUsedTime = 0.0;
    };

    struct EditorFrameDrawData
    {
        ImDrawData drawData;
        ImVector<ImTextureData*> textureRefs;
        ImPool<EditorDrawListSnapshotEntry> drawListCache;
        float memoryCompactTimer = 20.0f;

        ~EditorFrameDrawData()
        {
            Clear();
        }

        void Clear()
        {
            for (int cacheIndex = 0; cacheIndex < drawListCache.GetMapSize(); ++cacheIndex)
            {
                if (EditorDrawListSnapshotEntry* entry = drawListCache.TryGetMapData(cacheIndex); entry != nullptr)
                {
                    IM_DELETE(entry->snapshot);
                }
            }

            drawListCache.Clear();
            drawData.Clear();
            textureRefs.clear();
        }

        [[nodiscard]] bool SnapUsingSwap(ImDrawData* sourceDrawData, double currentTime)
        {
            IM_ASSERT(sourceDrawData != nullptr && sourceDrawData != &drawData && sourceDrawData->Valid);

            ImVector<ImDrawList*> sourceDrawLists;
            sourceDrawLists.swap(sourceDrawData->CmdLists);
            IM_ASSERT(sourceDrawData->CmdLists.Data == nullptr);
            drawData = *sourceDrawData;
            sourceDrawLists.swap(sourceDrawData->CmdLists);

            textureRefs.clear();
            bool hasTextureUpdates = false;
            if (sourceDrawData->Textures != nullptr)
            {
                for (ImTextureData* textureData : *sourceDrawData->Textures)
                {
                    if (textureData->Status != ImTextureStatus_OK)
                    {
                        textureRefs.push_back(textureData);
                        hasTextureUpdates = true;
                    }
                }
            }
            drawData.Textures = textureRefs.empty() ? nullptr : &textureRefs;

            for (ImDrawList* sourceDrawList : sourceDrawData->CmdLists)
            {
                const ImGuiID drawListID = ImHashData(&sourceDrawList, sizeof(sourceDrawList));
                EditorDrawListSnapshotEntry* entry = drawListCache.GetOrAddByKey(drawListID);
                if (entry->snapshot == nullptr)
                {
                    entry->source = sourceDrawList;
                    entry->snapshot = IM_NEW(ImDrawList)(sourceDrawList->_Data);
                }

                IM_ASSERT(entry->source == sourceDrawList);
                entry->source->CmdBuffer.swap(entry->snapshot->CmdBuffer);
                entry->source->IdxBuffer.swap(entry->snapshot->IdxBuffer);
                entry->source->VtxBuffer.swap(entry->snapshot->VtxBuffer);
                entry->source->CmdBuffer.reserve(entry->snapshot->CmdBuffer.Capacity);
                entry->source->IdxBuffer.reserve(entry->snapshot->IdxBuffer.Capacity);
                entry->source->VtxBuffer.reserve(entry->snapshot->VtxBuffer.Capacity);
                entry->lastUsedTime = currentTime;
                drawData.CmdLists.push_back(entry->snapshot);
            }

            const double garbageCollectThreshold = currentTime - memoryCompactTimer;
            for (int cacheIndex = 0; cacheIndex < drawListCache.GetMapSize(); ++cacheIndex)
            {
                EditorDrawListSnapshotEntry* entry = drawListCache.TryGetMapData(cacheIndex);
                if (entry == nullptr || entry->lastUsedTime > garbageCollectThreshold)
                {
                    continue;
                }

                const ImGuiID drawListID = ImHashData(&entry->source, sizeof(entry->source));
                IM_DELETE(entry->snapshot);
                drawListCache.Remove(drawListID, entry);
            }

            return hasTextureUpdates;
        }
    };

    struct EditorFrameRenderViews
    {
        std::shared_ptr<RTRenderTexture> sceneViewTexture;
        std::shared_ptr<RTCamera> sceneViewCameraSnapshot;
        std::shared_ptr<RTRenderViewState> sceneViewState;
        std::shared_ptr<RTCamera> gameViewCameraSnapshot;
        std::shared_ptr<RTRenderViewState> gameViewState;
        rhi::RhiFillMode sceneViewFillMode = rhi::RhiFillMode::Solid;
        bool sceneViewGridEnabled = false;
        Float32 sceneViewGridOpacity = 0.45f;
        Float32 sceneViewGridUnitSize = 1.0f;
        std::shared_ptr<EditorGizmoDrawList> sceneViewGizmoDrawList;
        std::shared_ptr<RTRenderTexture> gameViewTexture;
    };

    namespace
    {
        void AddAssetIDIfValid(std::vector<AssetID>& ids, const AssetID& id)
        {
            if (!id.IsEmpty() && std::find(ids.begin(), ids.end(), id) == ids.end())
            {
                ids.push_back(id);
            }
        }

        void CollectGameObjectResourceRoots(const GameObject& gameObject, std::vector<AssetID>& ids)
        {
            if (const MeshRenderComponent* meshRender = gameObject.GetComponent<MeshRenderComponent>(); meshRender != nullptr)
            {
                AddAssetIDIfValid(ids, meshRender->GetMeshAssetID());
                AddAssetIDIfValid(ids, meshRender->GetMaterialAssetID());
            }

            const TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
            if (transform == nullptr)
            {
                return;
            }

            for (SizeT childIndex = 0; childIndex < transform->GetChildCount(); ++childIndex)
            {
                const GameObject* child = transform->GetChildGameObject(childIndex);
                if (child != nullptr)
                {
                    CollectGameObjectResourceRoots(*child, ids);
                }
            }
        }

        void CollectSceneResourceRoots(const Scene& scene, std::vector<AssetID>& ids)
        {
            for (SizeT rootIndex = 0; rootIndex < scene.GetRootGameObjectCount(); ++rootIndex)
            {
                const GameObject* rootObject = scene.GetRootGameObject(rootIndex);
                if (rootObject != nullptr)
                {
                    CollectGameObjectResourceRoots(*rootObject, ids);
                }
            }
        }
    } // namespace

    Editor::Editor() = default;

    Editor::~Editor()
    {
        UnInit();
    }

    ErrorCode Editor::Init(EngineRuntime& runtime, ApplicationCommandQueue& mainThreadCommandQueue, void* nativeWindowHandle)
    {
        if (initialized_.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        if (nativeWindowHandle == nullptr)
        {
            return ErrorCode::InvalidArgument;
        }

        const ErrorCode toolchainResult = ValidateEditorToolchain();
        if (toolchainResult != ErrorCode::None)
        {
            return toolchainResult;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        VE_ASSERT_MESSAGE(ImGui::GetCurrentContext() != nullptr, "ImGui::CreateContext failed.");
        ConfigureImGuiIniFile();
        ImGui::StyleColorsDark();

        recentProjects_ = EditorProjectRegistry::LoadRecentProjects();

        const ErrorCode inputResult = input_.Init(nativeWindowHandle);
        VE_ASSERT(inputResult == ErrorCode::None);

        const ErrorCode renderBackendResult = InitRenderBackend(runtime.GetRenderSystem());
        if (renderBackendResult != ErrorCode::None)
        {
            input_.Shutdown();
            ImGui::DestroyContext();
            return renderBackendResult;
        }

        runtime_ = &runtime;
        renderSystem_ = &runtime.GetRenderSystem();
        mainThreadCommandQueue_ = &mainThreadCommandQueue;
        projectSelectionView_ = new ProjectSelectionView();
        projectEditingView_ = new ProjectEditingView();

        sceneSystem_ = &runtime.GetSceneSystem();
        nativeWindowHandle_ = nativeWindowHandle;
        initialized_.store(true, std::memory_order_release);
        sceneSystem_->SetEditorCallback(SceneSystemEditorCallback{
            .onBeforeOSEvents = [this]() { input_.BeginOSEventFrame(); },
            .onStartFrame = [this]() { StartFrame(); },
            .onOSEvent = [this](const OSEvent& event) { return HandleOSEvent(event); },
            .onRender = [this]() { return Render(); },
        });

        VE_LOG_INFO_CATEGORY("Editor", "Editor initialized.");
        return ErrorCode::None;
    }

    void Editor::StartFrame()
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_.load(std::memory_order_acquire))
        {
            return;
        }

        // The ImGui frame starts in Render so the context lock does not cover scene, script, or physics updates.
    }

    std::shared_ptr<FrameRenderPipeline> Editor::Render()
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_.load(std::memory_order_acquire))
        {
            return nullptr;
        }

        if (!pendingProjectPath_.empty())
        {
            std::string projectPath = std::move(pendingProjectPath_);
            pendingProjectPath_.clear();
            OpenProject(std::move(projectPath));
        }

        if (waitForImGuiTextureUpdates_)
        {
            renderSystem_->Flush();
            waitForImGuiTextureUpdates_ = false;
        }

        std::shared_ptr<EditorFrameDrawData> frameDrawData;
        {
            const std::scoped_lock lock(imguiContextMutex_);
            BeginRenderBackendFrame();
            input_.StartFrame();
            ImGui::NewFrame();
            RenderActiveMainView();
            frameDrawData = CaptureImGuiFrameDrawData();
        }
        EditorFrameRenderViews views = CollectFrameRenderViews();
        std::shared_ptr<RTScene> renderScene = GetActiveRenderScene();

        EditorRenderFramePipelineInitParam pipelineInitParam = {};
        if (views.sceneViewTexture != nullptr)
        {
            pipelineInitParam.retainedRenderTextures.push_back(views.sceneViewTexture);
        }
        if (views.gameViewTexture != nullptr)
        {
            pipelineInitParam.retainedRenderTextures.push_back(views.gameViewTexture);
        }
        AddSceneViewRenderer(pipelineInitParam, views, renderScene);
        AddGameViewRenderer(pipelineInitParam, views, renderScene);
        pipelineInitParam.overlayColorLoadAction = rhi::RhiLoadAction::Clear;
        pipelineInitParam.overlayRenderCallback = BuildOverlayRenderCallback(std::move(frameDrawData));
        return std::make_shared<EditorRenderFramePipeline>(std::move(pipelineInitParam));
    }

    void Editor::RenderActiveMainView()
    {
        switch (mainView_)
        {
        case MainView::ProjectSelection:
            projectSelectionView_->Render(*this);
            break;
        case MainView::ProjectEditing:
            projectEditingView_->Render(*this);
            break;
        }

        ImGui::Render();
    }

    bool Editor::HandleOSEvent(const OSEvent& event)
    {
        const std::scoped_lock lock(imguiContextMutex_);
        return input_.OnOSEvent(event);
    }

    std::shared_ptr<EditorFrameDrawData> Editor::CaptureImGuiFrameDrawData()
    {
        ImDrawData* sourceDrawData = ImGui::GetDrawData();
        VE_ASSERT_MESSAGE(sourceDrawData != nullptr && sourceDrawData->Valid, "Editor::Render requires valid ImGui draw data.");
        if (sourceDrawData == nullptr || !sourceDrawData->Valid)
        {
            return nullptr;
        }

        std::shared_ptr<EditorFrameDrawData>& snapshot = imguiDrawDataSnapshots_[nextImGuiDrawDataSnapshotIndex_];
        if (snapshot == nullptr || snapshot.use_count() != 1)
        {
            snapshot = std::make_shared<EditorFrameDrawData>();
        }

        waitForImGuiTextureUpdates_ = snapshot->SnapUsingSwap(sourceDrawData, ImGui::GetTime());
        std::shared_ptr<EditorFrameDrawData> frameDrawData = snapshot;
        nextImGuiDrawDataSnapshotIndex_ =
            static_cast<UInt32>((nextImGuiDrawDataSnapshotIndex_ + 1) % imguiDrawDataSnapshots_.size());
        return frameDrawData;
    }

    EditorFrameRenderViews Editor::CollectFrameRenderViews()
    {
        EditorFrameRenderViews views = {};
        if (mainView_ != MainView::ProjectEditing)
        {
            return views;
        }

        views.sceneViewTexture = projectEditingView_->GetSceneViewTexture();
        views.sceneViewCameraSnapshot = std::make_shared<RTCamera>(projectEditingView_->GetSceneViewCameraInitParam());
        views.sceneViewState = projectEditingView_->GetSceneRenderViewState()->GetRTRenderViewState();
        views.sceneViewFillMode = projectEditingView_->GetSceneViewFillMode();
        views.sceneViewGridEnabled = projectEditingView_->IsSceneViewGridEnabled();
        views.sceneViewGridOpacity = projectEditingView_->GetSceneViewGridOpacity();
        views.sceneViewGridUnitSize = projectEditingView_->GetSceneViewGridUnitSize();

        Scene* scene = sceneSystem_ != nullptr ? sceneSystem_->GetScene() : nullptr;
        views.sceneViewGizmoDrawList = projectEditingView_->GetSceneViewGizmos().BuildDrawList(GizmoBuildDesc{
            scene,
            GetSelectedGameObject(),
            projectEditingView_->GetSceneViewCameraLocalToWorld(),
        });
        views.gameViewTexture = projectEditingView_->GetGameViewTexture();
        CameraComponent* camera = scene != nullptr ? scene->GetCamera() : nullptr;
        views.gameViewCameraSnapshot = camera != nullptr ? camera->GetRTCamera() : nullptr;
        projectEditingView_->TrackGameViewCamera(views.gameViewCameraSnapshot);
        views.gameViewState = projectEditingView_->GetGameRenderViewState()->GetRTRenderViewState();
        VE_ASSERT_MESSAGE(views.sceneViewState != views.gameViewState, "Editor Scene and Game views require isolated persistent state.");
        return views;
    }

    EditorOverlayRenderCallback Editor::BuildOverlayRenderCallback(std::shared_ptr<EditorFrameDrawData> frameDrawData) const
    {
        EditorRenderBackend* renderBackend = editorRenderBackend_.get();
        std::mutex* imguiContextMutex = &imguiContextMutex_;
        return [renderBackend, editorInitialized = &initialized_, imguiContextMutex, frameDrawData = std::move(frameDrawData)](rhi::RhiCommandList& commandList)
        {
            VE_ASSERT_RENDER_THREAD();

            if (editorInitialized == nullptr || !editorInitialized->load(std::memory_order_acquire) || imguiContextMutex == nullptr ||
                frameDrawData == nullptr || renderBackend == nullptr)
            {
                return;
            }

            const std::scoped_lock lock(*imguiContextMutex);
            renderBackend->RenderDrawData(commandList, frameDrawData->drawData);
        };
    }

    void Editor::AddSceneViewRenderer(EditorRenderFramePipelineInitParam& pipelineInitParam,
                                      const EditorFrameRenderViews& views,
                                      const std::shared_ptr<RTScene>& renderScene) const
    {
        if (views.sceneViewTexture == nullptr || renderScene == nullptr)
        {
            return;
        }

        // Scene View is one renderer with a normal scene pass followed by editor-only visual aid passes.
        // Keeping the pass list together avoids repeating BaseRenderer setup for grid and gizmo overlays.
        StandaloneRendererInitParam rendererInitParam = {};
        rendererInitParam.scene = renderScene;
        rendererInitParam.camera = views.sceneViewCameraSnapshot;
        rendererInitParam.viewState = views.sceneViewState;
        rendererInitParam.target.colorTexture = views.sceneViewTexture;
        rendererInitParam.fillMode = views.sceneViewFillMode;
        rendererInitParam.target.colorLoadAction = rhi::RhiLoadAction::Clear;

        if (views.sceneViewGridEnabled)
        {
            SceneGridRenderPassInitParam gridPassInitParam = {};
            gridPassInitParam.opacity = views.sceneViewGridOpacity;
            gridPassInitParam.unitSize = views.sceneViewGridUnitSize;
            rendererInitParam.additionalPasses.push_back(std::make_unique<SceneGridRenderPass>(std::move(gridPassInitParam)));
        }

        if (views.sceneViewGizmoDrawList != nullptr)
        {
            EditorGizmoRenderPassInitParam gizmoPassInitParam = {};
            gizmoPassInitParam.drawList = views.sceneViewGizmoDrawList;
            rendererInitParam.additionalPasses.push_back(std::make_unique<EditorGizmoRenderPass>(std::move(gizmoPassInitParam)));
        }

        pipelineInitParam.sceneRenderers.push_back(std::move(rendererInitParam));
    }

    void Editor::AddGameViewRenderer(EditorRenderFramePipelineInitParam& pipelineInitParam,
                                     const EditorFrameRenderViews& views,
                                     const std::shared_ptr<RTScene>& renderScene) const
    {
        if (views.gameViewTexture == nullptr || renderScene == nullptr)
        {
            return;
        }

        // Game View intentionally stays free of editor gizmo/grid passes for now.
        StandaloneRendererInitParam rendererInitParam = {};
        rendererInitParam.scene = renderScene;
        rendererInitParam.camera = views.gameViewCameraSnapshot;
        rendererInitParam.viewState = views.gameViewState;
        rendererInitParam.target.colorTexture = views.gameViewTexture;
        pipelineInitParam.sceneRenderers.push_back(std::move(rendererInitParam));
    }

    std::shared_ptr<RTScene> Editor::GetActiveRenderScene() const
    {
        return sceneSystem_ != nullptr && sceneSystem_->GetScene() != nullptr ? sceneSystem_->GetScene()->GetRTScene() : nullptr;
    }

    void Editor::RequestRenderViewSceneChanges() noexcept
    {
        if (projectEditingView_ != nullptr)
        {
            projectEditingView_->RequestRenderViewSceneChanges();
        }
    }

    void Editor::UnInit() noexcept
    {
        if (!initialized_.load(std::memory_order_acquire))
        {
            return;
        }

        if (runtime_ != nullptr)
        {
            runtime_->GetTimeSystem().SetPaused(false);
        }

        initialized_.store(false, std::memory_order_release);
        if (sceneSystem_ != nullptr)
        {
            sceneSystem_->SetEditorCallback(SceneSystemEditorCallback{});
            sceneSystem_ = nullptr;
        }

        VE_ASSERT_MESSAGE(renderSystem_ != nullptr, "Editor::UnInit requires renderSystem_ to be valid.");
        if (renderSystem_->IsInitialized())
        {
            renderSystem_->WaitIdle();
        }

        for (std::shared_ptr<EditorFrameDrawData>& snapshot : imguiDrawDataSnapshots_)
        {
            snapshot.reset();
        }
        nextImGuiDrawDataSnapshotIndex_ = 0;
        waitForImGuiTextureUpdates_ = false;
        ShutdownRenderBackend();
        resourceLoader_.Shutdown();
        assetDatabase_.Shutdown();
        pendingProjectPath_.clear();

        delete projectSelectionView_;
        delete projectEditingView_;
        projectSelectionView_ = nullptr;
        projectEditingView_ = nullptr;

        input_.Shutdown();

        VE_ASSERT_MESSAGE(ImGui::GetCurrentContext() != nullptr, "Editor::UnInit requires an active ImGui context.");
        ImGui::DestroyContext();

        mainThreadCommandQueue_ = nullptr;
        nativeWindowHandle_ = nullptr;
        runtime_ = nullptr;
        renderSystem_ = nullptr;
        VE_LOG_INFO_CATEGORY("Editor", "Editor uninitialized.");
    }

    bool Editor::IsInitialized() const noexcept
    {
        return initialized_.load(std::memory_order_acquire);
    }

    SceneSystem& Editor::GetSceneSystem() noexcept
    {
        VE_ASSERT_MESSAGE(sceneSystem_ != nullptr, "Editor::GetSceneSystem requires an initialized editor.");
        return *sceneSystem_;
    }

    const SceneSystem& Editor::GetSceneSystem() const noexcept
    {
        VE_ASSERT_MESSAGE(sceneSystem_ != nullptr, "Editor::GetSceneSystem requires an initialized editor.");
        return *sceneSystem_;
    }

    EngineRuntime& Editor::GetRuntime() noexcept
    {
        VE_ASSERT_MESSAGE(runtime_ != nullptr, "Editor::GetRuntime requires an initialized editor.");
        return *runtime_;
    }

    const EngineRuntime& Editor::GetRuntime() const noexcept
    {
        VE_ASSERT_MESSAGE(runtime_ != nullptr, "Editor::GetRuntime requires an initialized editor.");
        return *runtime_;
    }

    RenderSystem& Editor::GetRenderSystem() noexcept
    {
        VE_ASSERT_MESSAGE(renderSystem_ != nullptr, "Editor::GetRenderSystem requires an initialized editor.");
        return *renderSystem_;
    }

    const EditorInput& Editor::GetInput() const noexcept
    {
        return input_;
    }

    EditorAssetDatabase& Editor::GetAssetDatabase() noexcept
    {
        return assetDatabase_;
    }

    const EditorAssetDatabase& Editor::GetAssetDatabase() const noexcept
    {
        return assetDatabase_;
    }

    EditorResourceLoader& Editor::GetResourceLoader() noexcept
    {
        return resourceLoader_;
    }

    const EditorResourceLoader& Editor::GetResourceLoader() const noexcept
    {
        return resourceLoader_;
    }

    EditorScriptDatabase& Editor::GetScriptDatabase() noexcept
    {
        return scriptDatabase_;
    }

    const EditorScriptDatabase& Editor::GetScriptDatabase() const noexcept
    {
        return scriptDatabase_;
    }

    EditorEventDispatcher& Editor::GetEventDispatcher() noexcept
    {
        return eventDispatcher_;
    }

    const EditorEventDispatcher& Editor::GetEventDispatcher() const noexcept
    {
        return eventDispatcher_;
    }

    void Editor::SetSelectedGameObject(ve::GameObject* gameObject)
    {
        selectionType_ = gameObject != nullptr ? EditorSelectionType::GameObject : EditorSelectionType::None;
        selectedGameObject_ = gameObject;
        selectedAssetPath_ = Path();
        CollectUnusedResources();
        DispatchSelectionChanged();
    }

    void Editor::SetSelectedAsset(Path assetPath)
    {
        selectionType_ = assetPath.IsEmpty() ? EditorSelectionType::None : EditorSelectionType::Asset;
        selectedGameObject_ = nullptr;
        selectedAssetPath_ = std::move(assetPath);
        CollectUnusedResources();
        DispatchSelectionChanged();
    }

    void Editor::ClearSelection()
    {
        selectionType_ = EditorSelectionType::None;
        selectedGameObject_ = nullptr;
        selectedAssetPath_ = Path();
        CollectUnusedResources();
        DispatchSelectionChanged();
    }

    EditorSelectionType Editor::GetSelectionType() const noexcept
    {
        return selectionType_;
    }

    EditorSelectionChangedEvent Editor::GetSelection() const
    {
        return EditorSelectionChangedEvent{
            .selectionType = selectionType_,
            .gameObject = selectionType_ == EditorSelectionType::GameObject ? selectedGameObject_ : nullptr,
            .assetPath = selectedAssetPath_,
        };
    }

    ve::GameObject* Editor::GetSelectedGameObject() noexcept
    {
        return selectionType_ == EditorSelectionType::GameObject ? selectedGameObject_ : nullptr;
    }

    const ve::GameObject* Editor::GetSelectedGameObject() const noexcept
    {
        return selectionType_ == EditorSelectionType::GameObject ? selectedGameObject_ : nullptr;
    }

    const Path& Editor::GetSelectedAssetPath() const noexcept
    {
        return selectedAssetPath_;
    }

    std::vector<AssetID> Editor::CollectActiveResourceRoots() const
    {
        std::vector<AssetID> roots;

        if (mainView_ == MainView::ProjectEditing && sceneSystem_ != nullptr && sceneSystem_->GetScene() != nullptr)
        {
            CollectSceneResourceRoots(*sceneSystem_->GetScene(), roots);
        }

        if (selectionType_ == EditorSelectionType::Asset)
        {
            const EditorAssetRecord* selectedAsset = assetDatabase_.FindAsset(selectedAssetPath_);
            if (selectedAsset != nullptr)
            {
                AddAssetIDIfValid(roots, selectedAsset->asset.id);
            }
        }

        return roots;
    }

    void Editor::CollectUnusedResources()
    {
        if (!resourceLoader_.IsInitialized())
        {
            return;
        }

        ResourceCollectUnusedParams params;
        params.rootAssets = CollectActiveResourceRoots();
        const SizeT unloadedCount = runtime_ != nullptr ? runtime_->GetResourceSystem().CollectUnusedResources(params) : 0;
        if (unloadedCount > 0)
        {
            VE_LOG_DEBUG_CATEGORY("Editor", "Collected {} unused resource(s).", unloadedCount);
        }
    }

    bool Editor::IsPlaying() const noexcept
    {
        return playState_ != EditorPlayState::Editing;
    }

    bool Editor::IsPaused() const noexcept
    {
        return playState_ == EditorPlayState::Paused;
    }

    bool Editor::CanTogglePause() const noexcept
    {
        return IsPlaying() && runtime_ != nullptr;
    }

    bool Editor::CanStepPlay() const noexcept
    {
        return IsPaused() && runtime_ != nullptr;
    }

    bool Editor::CanStartPlay() const noexcept
    {
        return playState_ == EditorPlayState::Editing && mainView_ == MainView::ProjectEditing && sceneSystem_ != nullptr &&
               sceneSystem_->GetScene() != nullptr && assetDatabase_.IsInitialized() && runtime_ != nullptr;
    }

    bool Editor::CanStopPlay() const noexcept
    {
        return IsPlaying() && sceneSystem_ != nullptr && runtime_ != nullptr && !editingSceneSnapshot_.empty();
    }

    void Editor::StartPlay()
    {
        if (!CanStartPlay())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Skipped Play because no editable scene is active.");
            return;
        }

        VE_ASSERT(sceneSystem_ != nullptr);
        VE_ASSERT(runtime_ != nullptr);

        Scene* editingScene = sceneSystem_->GetScene();
        VE_ASSERT(editingScene != nullptr);

        Result<std::string> snapshot = SceneSerialization::SaveToString(*editingScene);
        if (!snapshot)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to snapshot editing scene for Play: {}", snapshot.GetError().GetMessage());
            return;
        }

        ClearSelection();
        SceneLoadRequest loadRequest;
        loadRequest.source = SceneLoadSource::Text;
        loadRequest.sceneText = snapshot.GetValue();
        loadRequest.executionMode = SceneExecutionMode::Runtime;
        loadRequest.provider = &assetDatabase_;
        loadRequest.resourceSystem = &runtime_->GetResourceSystem();
        loadRequest.scriptingSystem = &runtime_->GetScriptingSystem();

        const ErrorCode physicsResetResult = runtime_->GetPhysicsSystem().ResetSimulation();
        if (physicsResetResult != ErrorCode::None)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to reset physics simulation before Play: {}", ToString(physicsResetResult));
            return;
        }

        Error loadResult = sceneSystem_->LoadScene(loadRequest);
        if (!loadResult.IsOk())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to enter Play mode: " + loadResult.GetMessage());
            return;
        }

        editingSceneSnapshot_ = snapshot.MoveValue();
        runtime_->GetTimeSystem().SetPaused(false);
        playState_ = EditorPlayState::Playing;
        ++playSessionID_;
        RequestRenderViewSceneChanges();
        CollectUnusedResources();
        VE_LOG_INFO_CATEGORY("Editor", "Entered Play mode.");
    }

    void Editor::StopPlay()
    {
        if (!CanStopPlay())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Skipped Stop because Play mode is not active.");
            return;
        }

        VE_ASSERT(sceneSystem_ != nullptr);
        VE_ASSERT(runtime_ != nullptr);

        ClearSelection();
        SceneLoadRequest loadRequest;
        loadRequest.source = SceneLoadSource::Text;
        loadRequest.sceneText = editingSceneSnapshot_;
        loadRequest.executionMode = SceneExecutionMode::Editing;
        loadRequest.provider = &assetDatabase_;
        loadRequest.resourceSystem = &runtime_->GetResourceSystem();
        loadRequest.scriptingSystem = &runtime_->GetScriptingSystem();
        Error loadResult = sceneSystem_->LoadScene(loadRequest);
        if (!loadResult.IsOk())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to restore editing scene after Play: " + loadResult.GetMessage());
            return;
        }

        runtime_->GetTimeSystem().SetPaused(false);
        editingSceneSnapshot_.clear();
        playState_ = EditorPlayState::Editing;
        ++playSessionID_;
        RequestRenderViewSceneChanges();
        CollectUnusedResources();
        VE_LOG_INFO_CATEGORY("Editor", "Exited Play mode.");
    }

    void Editor::TogglePause()
    {
        if (!CanTogglePause())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Skipped Pause because Play mode is not active.");
            return;
        }

        VE_ASSERT(runtime_ != nullptr);
        const bool shouldPause = playState_ == EditorPlayState::Playing;
        runtime_->GetTimeSystem().SetPaused(shouldPause);
        playState_ = shouldPause ? EditorPlayState::Paused : EditorPlayState::Playing;
        VE_LOG_INFO_CATEGORY("Editor", shouldPause ? "Paused Play mode." : "Resumed Play mode.");
    }

    void Editor::StepPlay()
    {
        if (!CanStepPlay())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Skipped Step because Play mode is not paused.");
            return;
        }

        VE_ASSERT(runtime_ != nullptr);
        if (!runtime_->GetTimeSystem().RequestStep())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to queue a paused Play step.");
        }
    }

    void Editor::ShutdownOpenProjectState() noexcept
    {
        if (runtime_ != nullptr)
        {
            runtime_->GetTimeSystem().SetPaused(false);
        }

        editingSceneSnapshot_.clear();
        playState_ = EditorPlayState::Editing;

        ClearSelection();

        if (sceneSystem_ != nullptr)
        {
            sceneSystem_->UnloadActiveScene();
        }

        resourceLoader_.Shutdown();
        assetDatabase_.Shutdown();
        scriptDatabase_.Clear();
        currentProjectPath_.clear();
        currentProjectName_.clear();
        currentScenePath_ = Path();
        mainView_ = MainView::ProjectSelection;
        EnqueueMainWindowTitleUpdate();
    }

    Result<EditorProjectDescriptor> Editor::PrepareOpenProjectDescriptor(const Path& projectRoot, const std::string& projectPath)
    {
        const ErrorCode layoutResult = EditorProject::EnsureLayout(projectRoot);
        if (layoutResult != ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY("Editor", "Failed to prepare project layout '{}': {}", projectPath, ToString(layoutResult));
            return Result<EditorProjectDescriptor>::Failure(Error(layoutResult, "Failed to prepare editor project layout."));
        }

        Result<EditorProjectDescriptor> descriptorResult = EditorProject::LoadDescriptor(projectRoot);
        if (!descriptorResult)
        {
            VE_LOG_ERROR_CATEGORY("Editor",
                                  "Failed to load project descriptor '{}': {}",
                                  EditorProject::GetDescriptorPath(projectRoot).GetString(),
                                  descriptorResult.GetError().GetMessage());
            return descriptorResult;
        }

        return descriptorResult;
    }

    ErrorCode Editor::InitializeOpenProjectAssetServices(const Path& projectRoot, const std::string& projectPath)
    {
        const ErrorCode assetDatabaseResult = assetDatabase_.Initialize(projectRoot);
        if (assetDatabaseResult != ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY("Editor", "Failed to initialize asset database '{}': {}", projectPath, ToString(assetDatabaseResult));
            return assetDatabaseResult;
        }

        const ErrorCode resourceLoaderResult = resourceLoader_.Initialize(projectRoot);
        if (resourceLoaderResult != ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY("Editor", "Failed to initialize resource loader '{}': {}", projectPath, ToString(resourceLoaderResult));
            assetDatabase_.Shutdown();
            return resourceLoaderResult;
        }

        return ErrorCode::None;
    }

    void Editor::ActivateOpenProjectContext(std::string projectPath, const Path& projectRoot, const EditorProjectDescriptor& descriptor)
    {
        FileSystem::SetProjectRoot(projectRoot);
        if (runtime_ != nullptr)
        {
            runtime_->GetResourceSystem().SetProjectRoot(projectRoot);
        }

        SetCurrentProject(std::move(projectPath));
        currentProjectName_ = descriptor.name.empty() ? currentProjectName_ : descriptor.name;
    }

    ErrorCode Editor::LoadScriptHostAssembly()
    {
        if (runtime_ == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        const Path& hostRoot = runtime_->GetScriptingSystem().GetScriptHostRoot();
        const Path hostAssemblyPath = hostRoot / "VEngine.ScriptHost.dll";
        if (!FileSystem::IsFile(hostAssemblyPath))
        {
            VE_LOG_WARN_CATEGORY("Editor", "VEngine.ScriptHost.dll was not found: {}", hostAssemblyPath.GetString());
            return ErrorCode::NotFound;
        }

        const ErrorCode result = runtime_->GetScriptingSystem().LoadAssembly(
            ScriptingAssemblyLoadDesc{hostAssemblyPath, "VEngine.Scripting.NativeScriptBridge, VEngine.ScriptHost"});
        if (result != ErrorCode::None)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to load VEngine.ScriptHost '{}': {}", hostAssemblyPath.GetString(), ToString(result));
            return result;
        }

        return ErrorCode::None;
    }

    void Editor::RecompileScripts()
    {
        if (IsPlaying())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Stop Play mode before recompiling scripts.");
            return;
        }

        if (currentProjectPath_.empty() || runtime_ == nullptr)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Open a project before recompiling scripts.");
            return;
        }

        if (assetDatabase_.IsInitialized())
        {
            const ErrorCode refreshResult = assetDatabase_.Refresh();
            if (refreshResult != ErrorCode::None)
            {
                VE_LOG_WARN_CATEGORY("Editor", "Failed to refresh assets before script compilation: {}", ToString(refreshResult));
                return;
            }
        }

        const ErrorCode hostResult = LoadScriptHostAssembly();
        if (hostResult != ErrorCode::None)
        {
            return;
        }

        const Path scriptHostAssemblyPath = runtime_->GetScriptingSystem().GetScriptHostRoot() / "VEngine.ScriptHost.dll";
        Result<EditorScriptCompileResult> compileResult = scriptCompiler_.CompileProjectScripts(EditorScriptCompileDesc{
            .projectRoot = Path(currentProjectPath_),
            .projectName = currentProjectName_,
            .scriptHostAssemblyPath = scriptHostAssemblyPath,
        });
        if (!compileResult)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Script compilation failed: {}", compileResult.GetError().GetMessage());
            return;
        }

        std::string sceneReloadSnapshot;
        if (sceneSystem_ != nullptr && sceneSystem_->GetScene() != nullptr)
        {
            Result<std::string> snapshot = SceneSerialization::SaveToString(*sceneSystem_->GetScene());
            if (!snapshot)
            {
                VE_LOG_WARN_CATEGORY("Editor", "Failed to snapshot editing scene before script reload: {}", snapshot.GetError().GetMessage());
                return;
            }

            sceneReloadSnapshot = snapshot.MoveValue();
        }

        const ErrorCode loadResult =
            runtime_->GetScriptingSystem().LoadProjectAssembly(ScriptingProjectAssemblyLoadDesc{compileResult.GetValue().assemblyPath});
        if (loadResult != ErrorCode::None)
        {
            VE_LOG_WARN_CATEGORY(
                "Editor", "Failed to load project script assembly '{}': {}", compileResult.GetValue().assemblyPath.GetString(), ToString(loadResult));
            return;
        }

        if (!sceneReloadSnapshot.empty() && sceneSystem_ != nullptr)
        {
            ClearSelection();
            SceneLoadRequest loadRequest;
            loadRequest.source = SceneLoadSource::Text;
            loadRequest.sceneText = sceneReloadSnapshot;
            loadRequest.executionMode = SceneExecutionMode::Editing;
            loadRequest.provider = &assetDatabase_;
            loadRequest.resourceSystem = &runtime_->GetResourceSystem();
            loadRequest.scriptingSystem = &runtime_->GetScriptingSystem();
            Error loadResult = sceneSystem_->LoadScene(loadRequest);
            if (!loadResult.IsOk())
            {
                VE_LOG_WARN_CATEGORY("Editor", "Failed to reload scene after script compile: " + loadResult.GetMessage());
                return;
            }
            RequestRenderViewSceneChanges();
        }

        const ErrorCode refreshResult = scriptDatabase_.RefreshFromScriptingSystem(runtime_->GetScriptingSystem());
        if (refreshResult != ErrorCode::None)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to refresh script type database: {}", ToString(refreshResult));
            return;
        }

        const Path manifestPath = compileResult.GetValue().outputDirectory / "ScriptAssembly.json";
        const ErrorCode saveManifestResult = scriptDatabase_.SaveManifest(manifestPath, compileResult.GetValue().assemblyPath);
        if (saveManifestResult != ErrorCode::None)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to write script assembly manifest '{}': {}", manifestPath.GetString(), ToString(saveManifestResult));
            return;
        }

        VE_LOG_INFO_CATEGORY("Editor", "Recompiled scripts. Script type count: {}", scriptDatabase_.GetScriptTypes().size());
    }

    Error Editor::LoadOpenProjectStartScene(const EditorProjectDescriptor& descriptor)
    {
        if (descriptor.startScene.empty())
        {
            return Error();
        }

        if (sceneSystem_ == nullptr || runtime_ == nullptr)
        {
            return Error(ErrorCode::InvalidState, "Runtime scene services are not ready for project start scene '" + descriptor.startScene + "'.");
        }

        const EditorAssetRecord* sceneAsset = assetDatabase_.FindAsset(Path(descriptor.startScene));
        if (sceneAsset == nullptr)
        {
            return Error(ErrorCode::NotFound, "Project start scene '" + descriptor.startScene + "' was not found in the asset database.");
        }

        if (sceneAsset->type != EditorAssetType::Scene)
        {
            return Error(ErrorCode::InvalidArgument, "Project start scene '" + descriptor.startScene + "' is not a scene asset.");
        }

        SceneLoadRequest loadRequest;
        loadRequest.source = SceneLoadSource::Asset;
        loadRequest.scene = sceneAsset->asset.id;
        loadRequest.executionMode = SceneExecutionMode::Editing;
        loadRequest.provider = &assetDatabase_;
        loadRequest.resourceSystem = &runtime_->GetResourceSystem();
        loadRequest.scriptingSystem = &runtime_->GetScriptingSystem();
        Error loadResult = sceneSystem_->LoadScene(loadRequest);
        if (!loadResult.IsOk())
        {
            return loadResult;
        }

        RequestRenderViewSceneChanges();
        currentScenePath_ = sceneAsset->path;
        return Error();
    }

    void Editor::EnterProjectEditingView()
    {
        AddRecentProject(currentProjectPath_);
        VE_ASSERT_MESSAGE(projectEditingView_ != nullptr, "Editor::OpenProject requires a project editing view.");
        projectEditingView_->Init(*this);
        mainView_ = MainView::ProjectEditing;
        CollectUnusedResources();
        EnqueueMainWindowTitleUpdate();
        VE_LOG_INFO_CATEGORY("Editor", "Opened editor project: {}", currentProjectPath_);
    }

    void Editor::RequestOpenProject(std::string projectPath)
    {
        if (!initialized_.load(std::memory_order_acquire) || projectPath.empty())
        {
            return;
        }

        pendingProjectPath_ = std::move(projectPath);
    }

    void Editor::OpenProject(std::string projectPath)
    {
        if (projectPath.empty())
        {
            return;
        }

        if (IsPlaying())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Stop Play mode before opening another project.");
            return;
        }

        const Path projectRoot(projectPath);

        // 1. Close the previous project before changing roots so existing AssetRefs release against the old resource
        // cache and the old asset database is no longer queried by editor panels.
        ShutdownOpenProjectState();

        // 2. Ensure the project layout and descriptor exist before any runtime/editor service points at the project.
        Result<EditorProjectDescriptor> descriptorResult = PrepareOpenProjectDescriptor(projectRoot, projectPath);
        if (!descriptorResult)
        {
            return;
        }

        // 3. Scan/import assets before scene construction. Scene loading needs the scene AssetID and component
        // dependencies resolved through EditorAssetDatabase records.
        const ErrorCode assetServicesResult = InitializeOpenProjectAssetServices(projectRoot, projectPath);
        if (assetServicesResult != ErrorCode::None)
        {
            return;
        }

        // 4. Activate project roots after asset services are valid and before ResourceSystem reads scene payloads.
        ActivateOpenProjectContext(std::move(projectPath), projectRoot, descriptorResult.GetValue());

        // 5. Compile and load C# scripts before scene construction so DotnetScriptableComponent can bind instances.
        RecompileScripts();

        // 6. Construct the live scene through SceneSystem so serialized AssetRefs are requested and bound.
        Error sceneLoadResult = LoadOpenProjectStartScene(descriptorResult.GetValue());
        if (!sceneLoadResult.IsOk())
        {
            VE_LOG_ERROR_CATEGORY("Editor", "Failed to open project start scene: " + sceneLoadResult.GetMessage());
            ShutdownOpenProjectState();
            EnqueueMainWindowTitleUpdate();
            return;
        }

        // 7. Only enter the editing UI after project services and the optional start scene have settled.
        EnterProjectEditingView();
    }

    void Editor::ShowProjectSelection()
    {
        mainView_ = MainView::ProjectSelection;
        ClearSelection();
    }

    void Editor::OpenScene(Path scenePath)
    {
        if (scenePath.IsEmpty())
        {
            return;
        }

        if (IsPlaying())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Stop Play mode before opening another scene.");
            return;
        }

        if (sceneSystem_ == nullptr || runtime_ == nullptr)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Skipped opening scene '{}' because runtime scene services are not ready.", scenePath.GetString());
            return;
        }

        const EditorAssetRecord* sceneAsset = assetDatabase_.FindAsset(scenePath);
        if (sceneAsset == nullptr)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Scene '{}' was not found in the asset database.", scenePath.GetString());
            return;
        }

        if (sceneAsset->type != EditorAssetType::Scene)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Asset '{}' is not a scene.", scenePath.GetString());
            return;
        }

        SceneLoadRequest loadRequest;
        loadRequest.source = SceneLoadSource::Asset;
        loadRequest.scene = sceneAsset->asset.id;
        loadRequest.executionMode = SceneExecutionMode::Editing;
        loadRequest.provider = &assetDatabase_;
        loadRequest.resourceSystem = &runtime_->GetResourceSystem();
        loadRequest.scriptingSystem = &runtime_->GetScriptingSystem();
        Error loadResult = sceneSystem_->LoadScene(loadRequest);
        if (!loadResult.IsOk())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to open scene '" + scenePath.GetString() + "': " + loadResult.GetMessage());
            return;
        }

        RequestRenderViewSceneChanges();
        currentScenePath_ = sceneAsset->path;
        ClearSelection();
        CollectUnusedResources();
        VE_LOG_INFO_CATEGORY("Editor", "Opened scene: {}", currentScenePath_.GetString());
    }

    void Editor::SaveCurrentScene()
    {
        if (!CanSaveCurrentScene())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Skipped saving scene because no editable scene is active.");
            return;
        }

        VE_ASSERT(sceneSystem_ != nullptr);
        const Scene* scene = sceneSystem_->GetScene();
        VE_ASSERT(scene != nullptr);

        Result<std::string> serializedScene = SceneSerialization::SaveToString(*scene);
        if (!serializedScene)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to serialize scene '{}': {}", currentScenePath_.GetString(), serializedScene.GetError().GetMessage());
            return;
        }

        const ErrorCode writeResult = FileSystem::WriteTextFile(assetDatabase_.GetProjectRoot() / currentScenePath_, serializedScene.GetValue());
        if (writeResult != ErrorCode::None)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to save scene '{}': {}", currentScenePath_.GetString(), ToString(writeResult));
            return;
        }

        const ErrorCode refreshResult = assetDatabase_.Refresh();
        if (refreshResult != ErrorCode::None)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Saved scene '{}' but failed to refresh asset database: {}", currentScenePath_.GetString(), ToString(refreshResult));
            return;
        }

        VE_LOG_INFO_CATEGORY("Editor", "Saved scene: {}", currentScenePath_.GetString());
    }

    bool Editor::CanSaveCurrentScene() const noexcept
    {
        return playState_ == EditorPlayState::Editing && sceneSystem_ != nullptr && sceneSystem_->GetScene() != nullptr && !currentScenePath_.IsEmpty() &&
               assetDatabase_.IsInitialized();
    }

    const std::string& Editor::GetCurrentProjectPath() const noexcept
    {
        return currentProjectPath_;
    }

    const std::string& Editor::GetCurrentProjectName() const noexcept
    {
        return currentProjectName_;
    }

    const Path& Editor::GetCurrentScenePath() const noexcept
    {
        return currentScenePath_;
    }

    const std::vector<std::string>& Editor::GetRecentProjects() const noexcept
    {
        return recentProjects_;
    }

    std::string Editor::GetProjectDisplayName(const std::string& projectPath)
    {
        if (projectPath.empty())
        {
            return "Untitled Project";
        }

        const size_t lastSeparator = projectPath.find_last_of("/\\");
        if (lastSeparator == std::string::npos || lastSeparator + 1 >= projectPath.size())
        {
            return projectPath;
        }

        return projectPath.substr(lastSeparator + 1);
    }

    void Editor::AddRecentProject(const std::string& projectPath)
    {
        recentProjects_.erase(std::remove(recentProjects_.begin(), recentProjects_.end(), projectPath), recentProjects_.end());
        recentProjects_.insert(recentProjects_.begin(), projectPath);
        if (recentProjects_.size() > EditorProjectRegistry::MaxRecentProjectCount)
        {
            recentProjects_.resize(EditorProjectRegistry::MaxRecentProjectCount);
        }

        EditorProjectRegistry::SaveRecentProjects(recentProjects_);
    }

    void Editor::SetCurrentProject(std::string projectPath)
    {
        currentProjectPath_ = std::move(projectPath);
        currentProjectName_ = GetProjectDisplayName(currentProjectPath_);
    }

    void Editor::DispatchSelectionChanged()
    {
        eventDispatcher_.Dispatch(GetSelection());
    }

    void Editor::EnqueueMainWindowTitleUpdate()
    {
        if (mainThreadCommandQueue_ == nullptr)
        {
            return;
        }

        void* nativeWindowHandle = nativeWindowHandle_;
        std::string title = BuildMainWindowTitle();
        mainThreadCommandQueue_->Enqueue([nativeWindowHandle, title = std::move(title)]() { ApplyMainWindowTitle(nativeWindowHandle, title); });
    }

    void Editor::ApplyMainWindowTitle(void* nativeWindowHandle, const std::string& title)
    {
#if VE_PLATFORM_WINDOWS
        if (nativeWindowHandle == nullptr)
        {
            return;
        }

        const int requiredLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title.data(), static_cast<int>(title.size()), nullptr, 0);
        if (requiredLength <= 0)
        {
            return;
        }

        std::wstring wideTitle(static_cast<size_t>(requiredLength), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title.data(), static_cast<int>(title.size()), wideTitle.data(), requiredLength);
        SetWindowTextW(static_cast<HWND>(nativeWindowHandle), wideTitle.c_str());
#else
#if VE_PLATFORM_MACOS
        ApplyMacMainWindowTitle(nativeWindowHandle, title);
#else
        (void)nativeWindowHandle;
        (void)title;
#endif
#endif
    }

    std::string Editor::BuildMainWindowTitle() const
    {
        std::string title = "VEngine Editor - ";
        title += GetRenderBackendName();
        if (!currentProjectPath_.empty())
        {
            title += " - ";
            title += currentProjectPath_;
        }

        return title;
    }

    const char* Editor::GetRenderBackendName() const noexcept
    {
        switch (renderBackend_)
        {
        case RenderBackend::D3D11:
            return "D3D11";
        case RenderBackend::D3D12:
            return "D3D12";
        case RenderBackend::Metal:
            return "Metal";
        }

        return "Unknown";
    }

    void Editor::ConfigureImGuiIniFile()
    {
        imguiIniFilename_.clear();

#if VE_PLATFORM_MACOS
        const char* homePath = std::getenv("HOME");

        if (homePath != nullptr && homePath[0] != '\0')
        {
            const Path settingsDirectory = Path(homePath) / "Library/Application Support/VEngine/Editor";
            const ErrorCode directoryResult = FileSystem::CreateDirectories(settingsDirectory);

            if (directoryResult == ErrorCode::None)
            {
                imguiIniFilename_ = (settingsDirectory / "imgui.ini").GetString();
            }
            else
            {
                VE_LOG_WARN_CATEGORY("Editor", "Failed to create ImGui settings directory '{}': {}", settingsDirectory.GetString(), ToString(directoryResult));
            }
        }

        if (imguiIniFilename_.empty())
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to locate a macOS home directory for ImGui settings.");
        }
#endif

        if (!imguiIniFilename_.empty())
        {
            ImGui::GetIO().IniFilename = imguiIniFilename_.c_str();
        }
    }

    ErrorCode Editor::InitRenderBackend(RenderSystem& renderSystem)
    {
#if VE_PLATFORM_WINDOWS
        editorRenderBackend_ = CreateWinEditorRenderBackend();
#elif VE_PLATFORM_MACOS
        editorRenderBackend_ = CreateMacEditorRenderBackend();
#else
        VE_LOG_WARN_CATEGORY("Editor", "Editor render backend is unsupported on this platform.");
        return ErrorCode::Unsupported;
#endif

        if (editorRenderBackend_ == nullptr)
        {
            return ErrorCode::Unsupported;
        }

        const ErrorCode initResult = editorRenderBackend_->Init(renderSystem);
        if (initResult != ErrorCode::None)
        {
            editorRenderBackend_.reset();
            return initResult;
        }

        renderBackend_ = editorRenderBackend_->GetBackend();
        return ErrorCode::None;
    }

    void Editor::BeginRenderBackendFrame()
    {
        if (editorRenderBackend_ == nullptr)
        {
            return;
        }

        editorRenderBackend_->BeginFrame();
    }

    void Editor::ShutdownRenderBackend() noexcept
    {
        if (editorRenderBackend_ == nullptr)
        {
            return;
        }

        editorRenderBackend_->Shutdown();
        editorRenderBackend_.reset();
    }

} // namespace ve::editor
