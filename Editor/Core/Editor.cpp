#include "Editor/Core/Editor.h"

#include "Editor/Core/EditorProjectEditingView.h"
#include "Editor/Core/EditorProjectSelectionView.h"
#include "Editor/Core/EditorProject.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Scene/SceneSerialization.h"
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

    class EditorImGuiRenderPass final : public RenderPass
    {
    public:
        EditorImGuiRenderPass(RenderBackend backend,
                              const std::atomic_bool& editorInitialized,
                              rhi::RhiLoadAction colorLoadAction,
                              std::shared_ptr<EditorFrameDrawData> frameDrawData)
            : backend_(backend)
            , editorInitialized_(&editorInitialized)
            , colorLoadAction_(colorLoadAction)
            , frameDrawData_(std::move(frameDrawData))
        {
        }

        [[nodiscard]] const char* GetName() const noexcept override
        {
            return "EditorImGuiPass";
        }

        void Setup(RenderPassBuilder& builder) override
        {
            builder.AddSwapchainColorAttachment(colorLoadAction_,
                                                rhi::RhiStoreAction::Store,
                                                rhi::RhiColor{0.05f, 0.07f, 0.10f, 1.0f});
        }

        void Execute(RenderPassContext& context) override
        {
            (void)context;
            VE_ASSERT_RENDER_THREAD();

            if (editorInitialized_ == nullptr || !editorInitialized_->load(std::memory_order_acquire) ||
                frameDrawData_ == nullptr)
            {
                return;
            }

            switch (backend_)
            {
            case RenderBackend::D3D11:
#if VE_PLATFORM_WINDOWS
                ImGui_ImplDX11_RenderDrawData(&frameDrawData_->drawData);
#endif
                break;
            case RenderBackend::D3D12:
            case RenderBackend::Metal:
                break;
            }
        }

    private:
        RenderBackend backend_ = RenderBackend::D3D12;
        const std::atomic_bool* editorInitialized_ = nullptr;
        rhi::RhiLoadAction colorLoadAction_ = rhi::RhiLoadAction::Clear;
        std::shared_ptr<EditorFrameDrawData> frameDrawData_;
    };

    namespace
    {
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

    ErrorCode Editor::Init(EngineRuntime& runtime,
                           ApplicationCommandQueue& mainThreadCommandQueue,
                           void* nativeWindowHandle)
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

    std::shared_ptr<FrameRenderer> Editor::Render()
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_.load(std::memory_order_acquire))
        {
            return nullptr;
        }

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

        std::shared_ptr<EditorFrameDrawData> frameDrawData = CloneFrameDrawData(ImGui::GetDrawData());
        VE_ASSERT_MESSAGE(frameDrawData != nullptr, "Editor::Render requires valid ImGui draw data.");

        auto renderer = std::make_shared<FrameRenderer>();

        const bool hasGameViewScenePass = mainView_ == MainView::ProjectEditing;
        if (hasGameViewScenePass)
        {
            std::shared_ptr<RTRenderTexture> gameViewTexture = projectEditingView_->GetGameViewTexture();
            renderer->AddPass(renderSystem_->CreateTriangleForwardPass(std::move(gameViewTexture)));
        }

        renderer->AddPass(std::make_unique<EditorImGuiRenderPass>(
            renderBackend_, initialized_, rhi::RhiLoadAction::Clear, std::move(frameDrawData)));
        return renderer;
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
        ErrorCode flushResult = renderSystem_->Flush();
        VE_ASSERT_MESSAGE(flushResult == ErrorCode::None || flushResult == ErrorCode::InvalidState,
                          "Editor::UnInit flush render queue failed.");
        retainedImGuiRenderTextures_.clear();

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

    RenderSystem& Editor::GetRenderSystem() noexcept
    {
        VE_ASSERT_MESSAGE(renderSystem_ != nullptr, "Editor::GetRenderSystem requires an initialized editor.");
        return *renderSystem_;
    }

    EditorAssetDatabase& Editor::GetAssetDatabase() noexcept
    {
        return assetDatabase_;
    }

    const EditorAssetDatabase& Editor::GetAssetDatabase() const noexcept
    {
        return assetDatabase_;
    }

    void Editor::SetSelectedGameObject(ve::GameObject* gameObject) noexcept
    {
        selectionType_ = gameObject != nullptr ? EditorSelectionType::GameObject : EditorSelectionType::None;
        selectedGameObject_ = gameObject;
        selectedAssetPath_ = Path();
    }

    void Editor::SetSelectedAsset(Path assetPath)
    {
        selectionType_ = assetPath.IsEmpty() ? EditorSelectionType::None : EditorSelectionType::Asset;
        selectedGameObject_ = nullptr;
        selectedAssetPath_ = std::move(assetPath);
    }

    void Editor::ClearSelection() noexcept
    {
        selectionType_ = EditorSelectionType::None;
        selectedGameObject_ = nullptr;
        selectedAssetPath_ = Path();
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
            const auto existing = std::find(retainedImGuiRenderTextures_.begin(),
                                            retainedImGuiRenderTextures_.end(),
                                            renderTexture);
            if (existing == retainedImGuiRenderTextures_.end())
            {
                retainedImGuiRenderTextures_.push_back(std::move(renderTexture));
            }
        }
    }

    void Editor::OpenProject(std::string projectPath)
    {
        if (projectPath.empty())
        {
            return;
        }

        const Path projectRoot(projectPath);
        const ErrorCode layoutResult = EditorProject::EnsureLayout(projectRoot);
        if (layoutResult != ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY(
                "Editor", "Failed to prepare project layout '{}': {}", projectPath, ToString(layoutResult));
            return;
        }

        Result<EditorProjectDescriptor> descriptorResult = EditorProject::LoadDescriptor(projectRoot);
        if (!descriptorResult)
        {
            VE_LOG_ERROR_CATEGORY("Editor",
                                  "Failed to load project descriptor '{}': {}",
                                  EditorProject::GetDescriptorPath(projectRoot).GetString(),
                                  descriptorResult.GetError().GetMessage());
            return;
        }

        FileSystem::SetProjectRoot(projectRoot);
        SetCurrentProject(std::move(projectPath));
        ClearSelection();
        currentProjectName_ =
            descriptorResult.GetValue().name.empty() ? currentProjectName_ : descriptorResult.GetValue().name;
        if (!descriptorResult.GetValue().startScene.empty() && sceneSystem_ != nullptr &&
            sceneSystem_->GetScene() != nullptr)
        {
            const ErrorCode loadSceneResult = SceneSerialization::LoadFromFile(
                *sceneSystem_->GetScene(), Path(descriptorResult.GetValue().startScene));
            if (loadSceneResult != ErrorCode::None)
            {
                VE_LOG_WARN_CATEGORY("Editor",
                                     "Failed to load project start scene '{}': {}",
                                     descriptorResult.GetValue().startScene,
                                     ToString(loadSceneResult));
            }
        }

        const ErrorCode assetDatabaseResult = assetDatabase_.Initialize(projectRoot);
        if (assetDatabaseResult != ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY(
                "Editor", "Failed to initialize asset database '{}': {}", projectPath, ToString(assetDatabaseResult));
            return;
        }

        if (runtime_ != nullptr && runtime_->GetResourceSystem().IsInitialized())
        {
            runtime_->GetResourceSystem().SetProjectRoot(projectRoot);
            assetDatabase_.RegisterResourceSystemCallbacks(runtime_->GetResourceSystem());
        }

        AddRecentProject(currentProjectPath_);
        VE_ASSERT_MESSAGE(projectEditingView_ != nullptr, "Editor::OpenProject requires a project editing view.");
        projectEditingView_->Init(*this);
        mainView_ = MainView::ProjectEditing;
        EnqueueMainWindowTitleUpdate();
        VE_LOG_INFO_CATEGORY("Editor", "Opened editor project: {}", currentProjectPath_);
    }

    void Editor::ShowProjectSelection() noexcept
    {
        ClearSelection();
        mainView_ = MainView::ProjectSelection;
    }

    const std::string& Editor::GetCurrentProjectPath() const noexcept
    {
        return currentProjectPath_;
    }

    const std::string& Editor::GetCurrentProjectName() const noexcept
    {
        return currentProjectName_;
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
        recentProjects_.erase(std::remove(recentProjects_.begin(), recentProjects_.end(), projectPath),
                              recentProjects_.end());
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
        mainThreadCommandQueue_->Enqueue(
            [nativeWindowHandle, title = std::move(title)]() { ApplyMainWindowTitle(nativeWindowHandle, title); });
    }

    void Editor::ApplyMainWindowTitle(void* nativeWindowHandle, const std::string& title)
    {
#if VE_PLATFORM_WINDOWS
        if (nativeWindowHandle == nullptr)
        {
            return;
        }

        const int requiredLength =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title.data(), static_cast<int>(title.size()), nullptr, 0);
        if (requiredLength <= 0)
        {
            return;
        }

        std::wstring wideTitle(static_cast<size_t>(requiredLength), L'\0');
        MultiByteToWideChar(CP_UTF8,
                            MB_ERR_INVALID_CHARS,
                            title.data(),
                            static_cast<int>(title.size()),
                            wideTitle.data(),
                            requiredLength);
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
            VE_ASSERT_MESSAGE(ImGui::GetCurrentContext() != nullptr,
                              "Editor::ShutdownRenderBackend requires an active ImGui context.");
            ImGui_ImplDX11_Shutdown();
#endif
            break;
        case RenderBackend::D3D12:
        case RenderBackend::Metal:
            VE_ASSERT_ALWAYS_MESSAGE(false,
                                     "Editor::ShutdownRenderBackend called for unsupported backend in current build.");
            break;
        }
    }
} // namespace ve::editor
