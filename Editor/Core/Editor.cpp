#include "Editor/Core/Editor.h"

#include "Editor/Core/EditorProject.h"
#include "Editor/Core/EditorProjectEditingView.h"
#include "Editor/Core/EditorProjectSelectionView.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Editor/RenderPass/EditorGizmoRenderPass.h"
#include "Editor/RenderPass/SceneGridRenderPass.h"
#include "Engine/Runtime/Scene/MeshRenderComponent.h"
#include "Engine/Runtime/Scene/SceneSerialization.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <imgui.h>

#if VE_PLATFORM_WINDOWS
#include <backends/imgui_impl_dx11.h>
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
#include <d3d11.h>
#endif

#include <algorithm>
#include <memory>
#include <utility>

namespace ve::editor
{
    struct EditorFrameDrawData
    {
        ImDrawData drawData;
        ImVector<ImTextureData*> textureRefs;
        std::vector<std::unique_ptr<ImDrawList>> ownedCmdLists;
    };

    struct EditorFrameRenderViews
    {
        std::shared_ptr<RTRenderTexture> sceneViewTexture;
        std::shared_ptr<RTCamera> sceneViewCameraSnapshot;
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

        [[nodiscard]] std::shared_ptr<EditorFrameDrawData> CloneFrameDrawData(const ImDrawData* sourceDrawData)
        {
            if (sourceDrawData == nullptr || !sourceDrawData->Valid)
            {
                return nullptr;
            }

            auto frameDrawData = std::make_shared<EditorFrameDrawData>();
            frameDrawData->drawData.Valid = sourceDrawData->Valid;
            frameDrawData->drawData.TotalIdxCount = 0;
            frameDrawData->drawData.TotalVtxCount = 0;
            frameDrawData->drawData.DisplayPos = sourceDrawData->DisplayPos;
            frameDrawData->drawData.DisplaySize = sourceDrawData->DisplaySize;
            frameDrawData->drawData.FramebufferScale = sourceDrawData->FramebufferScale;
            frameDrawData->drawData.OwnerViewport = sourceDrawData->OwnerViewport;
            frameDrawData->drawData.Textures = nullptr;

            if (sourceDrawData->Textures != nullptr)
            {
                frameDrawData->textureRefs.reserve(sourceDrawData->Textures->Size);
                for (int textureIndex = 0; textureIndex < sourceDrawData->Textures->Size; ++textureIndex)
                {
                    frameDrawData->textureRefs.push_back((*sourceDrawData->Textures)[textureIndex]);
                }
                frameDrawData->drawData.Textures = &frameDrawData->textureRefs;
            }

            frameDrawData->ownedCmdLists.reserve(static_cast<size_t>(sourceDrawData->CmdListsCount));
            for (int drawListIndex = 0; drawListIndex < sourceDrawData->CmdListsCount; ++drawListIndex)
            {
                ImDrawList* clonedDrawList = sourceDrawData->CmdLists[drawListIndex]->CloneOutput();
                frameDrawData->ownedCmdLists.emplace_back(clonedDrawList);
                frameDrawData->drawData.CmdLists.push_back(clonedDrawList);
                frameDrawData->drawData.TotalIdxCount += clonedDrawList->IdxBuffer.Size;
                frameDrawData->drawData.TotalVtxCount += clonedDrawList->VtxBuffer.Size;
            }

            frameDrawData->drawData.CmdListsCount = frameDrawData->drawData.CmdLists.Size;
            return frameDrawData;
        }
    } // namespace

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

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        VE_ASSERT_MESSAGE(ImGui::GetCurrentContext() != nullptr, "ImGui::CreateContext failed.");
        ImGui::StyleColorsDark();

        recentProjects_ = EditorProjectRegistry::LoadRecentProjects();

        const ErrorCode inputResult = input_.Init(nativeWindowHandle);
        VE_ASSERT(inputResult == ErrorCode::None);

        runtime_ = &runtime;
        renderSystem_ = &runtime.GetRenderSystem();
        mainThreadCommandQueue_ = &mainThreadCommandQueue;
        const ErrorCode renderBackendResult = InitRenderBackend(*renderSystem_);
        VE_ASSERT(renderBackendResult == ErrorCode::None);

        projectSelectionView_ = new ProjectSelectionView();
        projectEditingView_ = new ProjectEditingView();

        sceneSystem_ = &runtime.GetSceneSystem();
        nativeWindowHandle_ = nativeWindowHandle;
        initialized_.store(true, std::memory_order_release);
        sceneSystem_->SetEditorCallback(SceneSystemEditorCallback{
            .onBeforeOSEvents = [this]() { input_.BeginOSEventFrame(); },
            .onStartFrame = [this]() { StartFrame(); },
            .onOSEvent = [this](const OSEvent& event) { return input_.OnOSEvent(event); },
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

        switch (renderBackend_)
        {
        case RenderBackend::D3D11:
#if VE_PLATFORM_WINDOWS
            ImGui_ImplDX11_NewFrame();
#endif
            break;
        case RenderBackend::D3D12:
        case RenderBackend::Metal:
            break;
        }

        input_.StartFrame();
        ImGui::NewFrame();
    }

    std::shared_ptr<FrameRenderPipeline> Editor::Render()
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_.load(std::memory_order_acquire))
        {
            return nullptr;
        }

        RenderActiveMainView();

        std::shared_ptr<EditorFrameDrawData> frameDrawData = CaptureImGuiFrameDrawData();
        EditorFrameRenderViews views = CollectFrameRenderViews();
        std::shared_ptr<RTScene> renderScene = GetActiveRenderScene();

        EditorRenderFramePipelineInitParam pipelineInitParam = {};
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

    std::shared_ptr<EditorFrameDrawData> Editor::CaptureImGuiFrameDrawData() const
    {
        std::shared_ptr<EditorFrameDrawData> frameDrawData = CloneFrameDrawData(ImGui::GetDrawData());
        VE_ASSERT_MESSAGE(frameDrawData != nullptr, "Editor::Render requires valid ImGui draw data.");
        return frameDrawData;
    }

    EditorFrameRenderViews Editor::CollectFrameRenderViews() const
    {
        EditorFrameRenderViews views = {};
        if (mainView_ != MainView::ProjectEditing)
        {
            return views;
        }

        views.sceneViewTexture = projectEditingView_->GetSceneViewTexture();
        views.sceneViewCameraSnapshot = std::make_shared<RTCamera>(projectEditingView_->GetSceneViewCameraDesc());
        views.sceneViewFillMode = projectEditingView_->GetSceneViewFillMode();
        views.sceneViewGridEnabled = projectEditingView_->IsSceneViewGridEnabled();
        views.sceneViewGridOpacity = projectEditingView_->GetSceneViewGridOpacity();
        views.sceneViewGridUnitSize = projectEditingView_->GetSceneViewGridUnitSize();

        const Scene* scene = sceneSystem_ != nullptr ? sceneSystem_->GetScene() : nullptr;
        views.sceneViewGizmoDrawList = projectEditingView_->GetSceneViewGizmos().BuildDrawList(GizmoBuildDesc{
            scene,
            GetSelectedGameObject(),
            projectEditingView_->GetSceneViewCameraLocalToWorld(),
        });
        views.gameViewTexture = projectEditingView_->GetGameViewTexture();
        return views;
    }

    EditorOverlayRenderCallback Editor::BuildOverlayRenderCallback(std::shared_ptr<EditorFrameDrawData> frameDrawData) const
    {
        return [backend = renderBackend_, editorInitialized = &initialized_, frameDrawData = std::move(frameDrawData)]()
        {
            VE_ASSERT_RENDER_THREAD();

            if (editorInitialized == nullptr || !editorInitialized->load(std::memory_order_acquire) || frameDrawData == nullptr)
            {
                return;
            }

            switch (backend)
            {
            case RenderBackend::D3D11:
#if VE_PLATFORM_WINDOWS
                ImGui_ImplDX11_RenderDrawData(&frameDrawData->drawData);
#endif
                break;
            case RenderBackend::D3D12:
            case RenderBackend::Metal:
                break;
            }
        };
    }

    void Editor::AddSceneViewRenderer(EditorRenderFramePipelineInitParam& pipelineInitParam,
                                      const EditorFrameRenderViews& views,
                                      const std::shared_ptr<RTScene>& renderScene) const
    {
        if (views.sceneViewTexture == nullptr)
        {
            return;
        }

        // Scene View is one renderer with a normal scene pass followed by editor-only visual aid passes.
        // Keeping the pass list together avoids repeating BaseRenderer setup for grid and gizmo overlays.
        ForwardRendererInitParam rendererInitParam = {};
        rendererInitParam.scene = renderScene;
        rendererInitParam.externalCamera = views.sceneViewCameraSnapshot;
        rendererInitParam.target.colorTexture = views.sceneViewTexture;
        rendererInitParam.fillMode = views.sceneViewFillMode;
        rendererInitParam.target.colorLoadAction = rhi::RhiLoadAction::Load;
        rendererInitParam.target.colorStoreAction = rhi::RhiStoreAction::Store;

        if (views.sceneViewGridEnabled)
        {
            SceneGridRenderPassInitParam gridPassInitParam = {};
            gridPassInitParam.colorTexture = views.sceneViewTexture;
            gridPassInitParam.opacity = views.sceneViewGridOpacity;
            gridPassInitParam.unitSize = views.sceneViewGridUnitSize;
            rendererInitParam.passes.push_back(std::make_unique<SceneGridRenderPass>(std::move(gridPassInitParam)));
        }

        if (views.sceneViewGizmoDrawList != nullptr)
        {
            EditorGizmoRenderPassInitParam gizmoPassInitParam = {};
            gizmoPassInitParam.colorTexture = views.sceneViewTexture;
            gizmoPassInitParam.drawList = views.sceneViewGizmoDrawList;
            rendererInitParam.passes.push_back(std::make_unique<EditorGizmoRenderPass>(std::move(gizmoPassInitParam)));
        }

        pipelineInitParam.sceneRenderers.push_back(std::move(rendererInitParam));
    }

    void Editor::AddGameViewRenderer(EditorRenderFramePipelineInitParam& pipelineInitParam,
                                     const EditorFrameRenderViews& views,
                                     const std::shared_ptr<RTScene>& renderScene) const
    {
        if (views.gameViewTexture == nullptr)
        {
            return;
        }

        // Game View intentionally stays free of editor gizmo/grid passes for now.
        ForwardRendererInitParam rendererInitParam = {};
        rendererInitParam.scene = renderScene;
        rendererInitParam.target.colorTexture = views.gameViewTexture;
        pipelineInitParam.sceneRenderers.push_back(std::move(rendererInitParam));
    }

    std::shared_ptr<RTScene> Editor::GetActiveRenderScene() const
    {
        return sceneSystem_ != nullptr && sceneSystem_->GetScene() != nullptr ? sceneSystem_->GetScene()->GetRTScene() : nullptr;
    }

    void Editor::UnInit() noexcept
    {
        if (!initialized_.load(std::memory_order_acquire))
        {
            return;
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
            renderSystem_->Flush();
        }
        retainedImGuiRenderTextures_.clear();
        resourceLoader_.Shutdown();
        assetDatabase_.Shutdown();

        delete projectSelectionView_;
        delete projectEditingView_;
        projectSelectionView_ = nullptr;
        projectEditingView_ = nullptr;

        ShutdownRenderBackend();

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

    void Editor::SetSelectedGameObject(ve::GameObject* gameObject)
    {
        selectionType_ = gameObject != nullptr ? EditorSelectionType::GameObject : EditorSelectionType::None;
        selectedGameObject_ = gameObject;
        selectedAssetPath_ = Path();
        CollectUnusedResources();
    }

    void Editor::SetSelectedAsset(Path assetPath)
    {
        selectionType_ = assetPath.IsEmpty() ? EditorSelectionType::None : EditorSelectionType::Asset;
        selectedGameObject_ = nullptr;
        selectedAssetPath_ = std::move(assetPath);
        CollectUnusedResources();
    }

    void Editor::ClearSelection()
    {
        selectionType_ = EditorSelectionType::None;
        selectedGameObject_ = nullptr;
        selectedAssetPath_ = Path();
        CollectUnusedResources();
    }

    EditorSelectionType Editor::GetSelectionType() const noexcept
    {
        return selectionType_;
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

    void Editor::KeepImGuiTextureAlive(std::shared_ptr<RenderTexture> renderTexture)
    {
        VE_ASSERT_SCENE_THREAD();
        if (renderTexture != nullptr)
        {
            const auto existing = std::find(retainedImGuiRenderTextures_.begin(), retainedImGuiRenderTextures_.end(), renderTexture);
            if (existing == retainedImGuiRenderTextures_.end())
            {
                retainedImGuiRenderTextures_.push_back(std::move(renderTexture));
            }
        }
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

    void Editor::ShutdownOpenProjectState() noexcept
    {
        ClearSelection();

        if (sceneSystem_ != nullptr)
        {
            sceneSystem_->UnloadActiveScene();
        }

        resourceLoader_.Shutdown();
        assetDatabase_.Shutdown();
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

    void Editor::LoadOpenProjectStartScene(const EditorProjectDescriptor& descriptor)
    {
        if (descriptor.startScene.empty())
        {
            return;
        }

        if (sceneSystem_ == nullptr || runtime_ == nullptr)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Skipped project start scene '{}' because runtime scene services are not ready.", descriptor.startScene);
            return;
        }

        const EditorAssetRecord* sceneAsset = assetDatabase_.FindAsset(Path(descriptor.startScene));
        if (sceneAsset == nullptr)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Project start scene '{}' was not found in the asset database.", descriptor.startScene);
            return;
        }

        if (sceneAsset->type != EditorAssetType::Scene)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Project start scene '{}' is not a scene asset.", descriptor.startScene);
            return;
        }

        Result<Scene*> sceneResult =
            sceneSystem_->LoadScene(SceneLoadDesc{sceneAsset->asset.id, SceneLoadMode::Single}, assetDatabase_, runtime_->GetResourceSystem());
        if (!sceneResult)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to construct project start scene '{}': {}", descriptor.startScene, sceneResult.GetError().GetMessage());
            currentScenePath_ = Path();
            return;
        }

        currentScenePath_ = sceneAsset->path;
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

    void Editor::OpenProject(std::string projectPath)
    {
        if (projectPath.empty())
        {
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

        // 5. Construct the live scene through SceneSystem so serialized AssetRefs are requested and bound.
        LoadOpenProjectStartScene(descriptorResult.GetValue());

        // 6. Only enter the editing UI after project services and the optional start scene have settled.
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

        Result<Scene*> sceneResult =
            sceneSystem_->LoadScene(SceneLoadDesc{sceneAsset->asset.id, SceneLoadMode::Single}, assetDatabase_, runtime_->GetResourceSystem());
        if (!sceneResult)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to open scene '{}': {}", scenePath.GetString(), sceneResult.GetError().GetMessage());
            return;
        }

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
        return sceneSystem_ != nullptr && sceneSystem_->GetScene() != nullptr && !currentScenePath_.IsEmpty() && assetDatabase_.IsInitialized();
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
        (void)nativeWindowHandle;
        (void)title;
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

    ErrorCode Editor::InitRenderBackend(RenderSystem& renderSystem)
    {
        RenderNativeHandles nativeHandles;
        const ErrorCode queryResult = renderSystem.QueryNativeHandles(nativeHandles);
        if (queryResult != ErrorCode::None)
        {
            return queryResult;
        }

        if (!nativeHandles.hasMainSwapchain)
        {
            return ErrorCode::InvalidState;
        }

        renderBackend_ = nativeHandles.backend;
        switch (nativeHandles.backend)
        {
        case RenderBackend::D3D11:
        {
#if VE_PLATFORM_WINDOWS
            auto* nativeDevice = static_cast<ID3D11Device*>(nativeHandles.device);
            auto* nativeImmediateContext = static_cast<ID3D11DeviceContext*>(nativeHandles.immediateContext);
            if (nativeDevice == nullptr || nativeImmediateContext == nullptr)
            {
                return ErrorCode::InvalidState;
            }

            if (!ImGui_ImplDX11_Init(nativeDevice, nativeImmediateContext))
            {
                return ErrorCode::PlatformError;
            }

            return ErrorCode::None;
#else
            return ErrorCode::Unsupported;
#endif
        }
        case RenderBackend::D3D12:
            VE_LOG_WARN_CATEGORY("Editor", "ImGui D3D12 backend initialization is not implemented yet.");
            return ErrorCode::Unsupported;
        case RenderBackend::Metal:
            VE_LOG_WARN_CATEGORY("Editor", "ImGui Metal backend initialization is not implemented yet.");
            return ErrorCode::Unsupported;
        }

        return ErrorCode::Unsupported;
    }

    void Editor::ShutdownRenderBackend() noexcept
    {
        switch (renderBackend_)
        {
        case RenderBackend::D3D11:
#if VE_PLATFORM_WINDOWS
            VE_ASSERT_MESSAGE(ImGui::GetCurrentContext() != nullptr, "Editor::ShutdownRenderBackend requires an active ImGui context.");
            ImGui_ImplDX11_Shutdown();
#endif
            break;
        case RenderBackend::D3D12:
        case RenderBackend::Metal:
            VE_ASSERT_ALWAYS_MESSAGE(false, "Editor::ShutdownRenderBackend called for unsupported backend in current build.");
            break;
        }
    }
} // namespace ve::editor
