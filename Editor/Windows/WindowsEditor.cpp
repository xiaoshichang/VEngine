#include "Editor/Core/EditorProject.h"
#include "Editor/Windows/WindowsEditorPanels.h"
#include "Editor/Windows/WindowsProjectLauncher.h"

#include "Engine/Runtime/Application/Application.h"
#include "Engine/Runtime/Core/BuildConfig.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Platform/Windows/Win32DebugConsole.h"
#include "Engine/Runtime/Platform/Windows/Win32Window.h"
#include "Engine/Runtime/Render/EditorUiFrame.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Tools/Package/PackageService.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <Objbase.h>
#include <Shellapi.h>
#include <ShlObj.h>

#include <imgui.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window,
                                                             UINT message,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace
{
    class WindowsComScope
    {
    public:
        WindowsComScope() noexcept
            : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))
        {
        }

        ~WindowsComScope()
        {
            if (SUCCEEDED(result_))
            {
                CoUninitialize();
            }
        }

        [[nodiscard]] bool IsAvailable() const noexcept
        {
            return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
        }

    private:
        HRESULT result_ = E_FAIL;
    };

    [[nodiscard]] std::wstring Utf8ToWide(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int requiredLength = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (requiredLength <= 0)
        {
            return {};
        }

        std::wstring wideText(static_cast<size_t>(requiredLength), L'\0');
        MultiByteToWideChar(CP_UTF8,
                            MB_ERR_INVALID_CHARS,
                            text.data(),
                            static_cast<int>(text.size()),
                            wideText.data(),
                            requiredLength);
        return wideText;
    }

    [[nodiscard]] std::string WideToUtf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int requiredLength = WideCharToMultiByte(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (requiredLength <= 0)
        {
            return {};
        }

        std::string utf8Text(static_cast<size_t>(requiredLength), '\0');
        WideCharToMultiByte(CP_UTF8,
                            0,
                            text.data(),
                            static_cast<int>(text.size()),
                            utf8Text.data(),
                            requiredLength,
                            nullptr,
                            nullptr);
        return utf8Text;
    }

    void ShowErrorMessage(HWND owner, std::string_view message)
    {
        const std::wstring wideMessage = Utf8ToWide(message);
        MessageBoxW(owner, wideMessage.c_str(), L"VEngine Editor", MB_OK | MB_ICONERROR);
    }

    [[nodiscard]] ve::Path ParseProjectArgument(PWSTR commandLine)
    {
        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(commandLine, &argumentCount);
        if (arguments == nullptr)
        {
            return {};
        }

        ve::Path projectRoot;
        for (int index = 0; index < argumentCount; ++index)
        {
            const std::wstring_view argument(arguments[index]);
            if (argument == L"--project" && index + 1 < argumentCount)
            {
                projectRoot = ve::Path(WideToUtf8(arguments[index + 1]));
                break;
            }
        }

        LocalFree(arguments);
        return projectRoot;
    }

    [[nodiscard]] std::string MakeProjectDisplayName(const ve::Path& projectRoot)
    {
        std::string displayName = projectRoot.GetFilename();
        return displayName.empty() ? "VEngineProject" : displayName;
    }

    [[nodiscard]] std::string MakeRecentProjectTitle(const ve::WindowsRecentProject& recentProject)
    {
        std::string title = recentProject.displayName.empty() ? recentProject.path.GetFilename()
                                                              : recentProject.displayName;
        return title.empty() ? "Untitled Project" : title;
    }

    [[nodiscard]] std::string MakeProjectOpenError(std::string_view prefix, ve::ErrorCode result)
    {
        std::string message(prefix);
        message += ": ";
        message += ve::ToString(result);
        return message;
    }

    [[nodiscard]] ve::PackageConfiguration GetEditorPackageConfiguration() noexcept
    {
#if VE_BUILD_DEBUG
        return ve::PackageConfiguration::Debug;
#else
        return ve::PackageConfiguration::Release;
#endif
    }

    [[nodiscard]] ve::ErrorCode OpenEditorProject(ve::EditorProjectService& projectService,
                                                  ve::EngineRuntime& runtime,
                                                  const ve::Path& projectRoot,
                                                  bool updateRecentProjects)
    {
        ve::GameThreadSystem& gameThreadSystem = runtime.GetGameThreadSystem();
        gameThreadSystem.ClearActiveScene();

        ve::ErrorCode openResult = projectService.OpenProject(projectRoot, runtime.GetResourceManager());
        if (openResult != ve::ErrorCode::None)
        {
            return openResult;
        }

        if (updateRecentProjects)
        {
            const ve::ErrorCode recentResult =
                ve::SaveWindowsRecentProject(projectService.GetProjectRoot(), projectService.GetDescriptor());
            if (recentResult != ve::ErrorCode::None)
            {
                VE_LOG_WARN_CATEGORY("Editor", "Failed to update recent projects: {}", ve::ToString(recentResult));
            }
        }

        return ve::ErrorCode::None;
    }

    class WindowsProjectLauncherUi;

    [[nodiscard]] bool TryOpenProjectSelection(WindowsProjectLauncherUi& launcherUi,
                                               ve::EditorProjectService& projectService,
                                               ve::EngineRuntime& runtime,
                                               ve::Window& window,
                                               const ve::WindowsProjectLauncherResult& selection,
                                               bool reportInLauncher);

    class WindowsProjectLauncherUi
    {
    public:
        WindowsProjectLauncherUi() = default;
        ~WindowsProjectLauncherUi()
        {
            Shutdown();
        }

        WindowsProjectLauncherUi(const WindowsProjectLauncherUi&) = delete;
        WindowsProjectLauncherUi& operator=(const WindowsProjectLauncherUi&) = delete;

        [[nodiscard]] bool IsInitialized() const noexcept
        {
            return initialized_;
        }

        [[nodiscard]] ve::ErrorCode Initialize(HWND window)
        {
            if (initialized_)
            {
                return ve::ErrorCode::None;
            }

            window_ = window;
            recentProjects_ = ve::LoadWindowsRecentProjects();

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
            io.BackendRendererName = "VEngine RHI ImGui Renderer";
            io.IniFilename = nullptr;

            ImGui::StyleColorsDark();
            ApplyStyle();

            if (!PrepareFontAtlas())
            {
                ImGui::DestroyContext();
                window_ = nullptr;
                lastError_ = "ImGui font atlas preparation failed.";
                return ve::ErrorCode::PlatformError;
            }

            if (!ImGui_ImplWin32_Init(window_))
            {
                ImGui::DestroyContext();
                window_ = nullptr;
                lastError_ = "ImGui Win32 backend initialization failed.";
                return ve::ErrorCode::PlatformError;
            }

            initialized_ = true;
            return ve::ErrorCode::None;
        }

        void Shutdown() noexcept
        {
            if (initialized_)
            {
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();
            }

            initialized_ = false;
            fontAtlasSubmitted_ = false;
            window_ = nullptr;
            fontAtlas_ = {};
            ResetEditorSelection();
        }

        [[nodiscard]] bool
        HandleWin32Message(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) noexcept
        {
            if (!initialized_)
            {
                return false;
            }

            result = ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
            return result != 0;
        }

        [[nodiscard]] std::optional<ve::WindowsProjectLauncherResult> Render(HWND owner, ve::RenderSystem& renderSystem)
        {
            if (!initialized_)
            {
                return std::nullopt;
            }

            if (IsIconic(window_) != FALSE)
            {
                return std::nullopt;
            }

            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            std::optional<ve::WindowsProjectLauncherResult> result = DrawLauncher(owner);

            ImGui::Render();
            ve::EditorUiFrameData frameData = CaptureEditorUiFrame(!fontAtlasSubmitted_);
            renderSystem.SubmitEditorUiFrame(std::move(frameData));
            fontAtlasSubmitted_ = true;

            return result;
        }

        void RenderEditor(HWND owner,
                          ve::Window& window,
                          ve::EditorProjectService& projectService,
                          ve::EngineRuntime& runtime)
        {
            if (!initialized_)
            {
                return;
            }

            if (IsIconic(window_) != FALSE)
            {
                return;
            }

            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            projectService.TickPlayMode();
            editorPanels_.BeginFrame();
            DrawEditor(owner, window, projectService, runtime);

            ImGui::Render();
            ve::EditorUiFrameData frameData = CaptureEditorUiFrame(!fontAtlasSubmitted_);
            runtime.GetRenderSystem().SubmitEditorUiFrame(std::move(frameData));
            fontAtlasSubmitted_ = true;

            runtime.GetRenderSystem().SubmitEditorViewportFrame(editorPanels_.ConsumeViewportFrameData(),
                                                                 runtime.GetResourceManager());
        }

        void RefreshRecentProjects()
        {
            recentProjects_ = ve::LoadWindowsRecentProjects();
        }

        void SetError(std::string message)
        {
            pendingError_ = std::move(message);
        }

        void SetStatus(std::string message)
        {
            statusMessage_ = std::move(message);
        }

        void ResetEditorSelection() noexcept
        {
            editorPanels_.ResetSelection();
            pendingAction_ = {};
            openDirtyScenePopup_ = false;
        }

        void RequestProjectOpen(ve::WindowsProjectLauncherResult selection, bool reportInLauncher)
        {
            pendingAction_ = {};
            pendingAction_.kind = PendingActionKind::OpenProject;
            pendingAction_.projectSelection = std::move(selection);
            pendingAction_.reportInLauncher = reportInLauncher;
            openDirtyScenePopup_ = true;
        }

        void SaveCurrentScene(ve::EditorProjectService& projectService, ve::EngineRuntime& runtime)
        {
            runtime.GetGameThreadSystem().ClearActiveScene();
            const ve::ErrorCode saveResult = projectService.SaveCurrentScene();
            if (saveResult == ve::ErrorCode::None)
            {
                statusMessage_ = "Scene saved: " + projectService.GetCurrentScenePath().GetString();
                return;
            }

            statusMessage_ = MakeProjectOpenError("Save Scene failed", saveResult);
        }

        void PackageCurrentProject(ve::EditorProjectService& projectService)
        {
            if (!projectService.HasOpenProject())
            {
                statusMessage_ = "Package Project failed: no project is open.";
                return;
            }

            const ve::PackageConfiguration configuration = GetEditorPackageConfiguration();

            ve::PackageRequest request;
            request.projectRoot = projectService.GetProjectRoot();
            request.outputRoot = projectService.GetProjectRoot() / "Generated/Build/Windows" /
                                 ve::ToString(configuration);
            request.playerExecutable = ve::FileSystem::GetExecutableDirectory() / "VEnginePlayer.exe";
            request.platform = ve::PackagePlatform::Windows;
            request.configuration = configuration;
            request.includeRuntimeBinaries = true;

            ve::Result<ve::PackageResult> packageResult = ve::StagePackage(request);
            if (!packageResult)
            {
                statusMessage_ = "Package Project failed: " + packageResult.GetError().GetMessage();
                VE_LOG_ERROR_CATEGORY("Editor", "{}", statusMessage_);
                return;
            }

            statusMessage_ = "Package staged: " + packageResult.GetValue().packageRoot.GetString();
            VE_LOG_INFO_CATEGORY("Editor",
                                 "Packaged project '{}' to '{}'",
                                 projectService.GetDescriptor().displayName,
                                 packageResult.GetValue().packageRoot.GetString());
        }

        [[nodiscard]] const std::string& GetLastError() const noexcept
        {
            return lastError_;
        }

    private:
        enum class PendingActionKind
        {
            None,
            OpenScene,
            OpenProject,
        };

        struct PendingAction
        {
            PendingActionKind kind = PendingActionKind::None;
            ve::Path scenePath;
            ve::WindowsProjectLauncherResult projectSelection;
            bool reportInLauncher = false;
        };

        void ApplyStyle()
        {
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.FrameRounding = 4.0f;
            style.ChildRounding = 6.0f;
            style.PopupRounding = 4.0f;
            style.ScrollbarRounding = 4.0f;
            style.GrabRounding = 4.0f;
            style.WindowBorderSize = 0.0f;
            style.FrameBorderSize = 1.0f;
            style.ItemSpacing = ImVec2(10.0f, 8.0f);
            style.WindowPadding = ImVec2(28.0f, 26.0f);

            ImVec4* colors = style.Colors;
            colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.06f, 0.065f, 1.0f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.08f, 0.09f, 1.0f);
            colors[ImGuiCol_Border] = ImVec4(0.22f, 0.24f, 0.27f, 1.0f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.13f, 0.145f, 1.0f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.17f, 0.19f, 0.21f, 1.0f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.24f, 0.27f, 1.0f);
            colors[ImGuiCol_Button] = ImVec4(0.16f, 0.19f, 0.21f, 1.0f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.27f, 0.30f, 1.0f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.27f, 0.33f, 0.36f, 1.0f);
            colors[ImGuiCol_Header] = ImVec4(0.14f, 0.18f, 0.20f, 1.0f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.25f, 0.28f, 1.0f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.30f, 0.33f, 1.0f);
        }

        [[nodiscard]] bool PrepareFontAtlas()
        {
            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ImGui::GetIO().Fonts->SetTexID(static_cast<ImTextureID>(1));

            if (pixels == nullptr || width <= 0 || height <= 0)
            {
                return false;
            }

            const size_t pixelByteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
            fontAtlas_.width = static_cast<ve::UInt32>(width);
            fontAtlas_.height = static_cast<ve::UInt32>(height);
            fontAtlas_.rgbaPixels.assign(pixels, pixels + pixelByteCount);
            return true;
        }

        [[nodiscard]] ve::EditorUiFrameData CaptureEditorUiFrame(bool includeFontAtlas)
        {
            ve::EditorUiFrameData frameData;

            ImDrawData* drawData = ImGui::GetDrawData();
            if (drawData == nullptr)
            {
                return frameData;
            }

            frameData.displayPos[0] = drawData->DisplayPos.x;
            frameData.displayPos[1] = drawData->DisplayPos.y;
            frameData.displaySize[0] = drawData->DisplaySize.x;
            frameData.displaySize[1] = drawData->DisplaySize.y;
            frameData.framebufferScale[0] = drawData->FramebufferScale.x;
            frameData.framebufferScale[1] = drawData->FramebufferScale.y;

            if (includeFontAtlas)
            {
                frameData.fontAtlas = fontAtlas_;
            }

            frameData.drawLists.reserve(static_cast<size_t>(drawData->CmdListsCount));
            for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
            {
                const ImDrawList* sourceList = drawData->CmdLists[listIndex];
                if (sourceList == nullptr)
                {
                    continue;
                }

                ve::EditorUiDrawList drawList;
                drawList.vertices.reserve(static_cast<size_t>(sourceList->VtxBuffer.Size));
                drawList.indices.reserve(static_cast<size_t>(sourceList->IdxBuffer.Size));
                drawList.commands.reserve(static_cast<size_t>(sourceList->CmdBuffer.Size));

                for (const ImDrawVert& sourceVertex : sourceList->VtxBuffer)
                {
                    ve::EditorUiVertex vertex;
                    vertex.position[0] = sourceVertex.pos.x;
                    vertex.position[1] = sourceVertex.pos.y;
                    vertex.uv[0] = sourceVertex.uv.x;
                    vertex.uv[1] = sourceVertex.uv.y;
                    vertex.color = sourceVertex.col;
                    drawList.vertices.push_back(vertex);
                }

                for (ImDrawIdx sourceIndex : sourceList->IdxBuffer)
                {
                    drawList.indices.push_back(static_cast<ve::UInt32>(sourceIndex));
                }

                for (const ImDrawCmd& sourceCommand : sourceList->CmdBuffer)
                {
                    if (sourceCommand.UserCallback != nullptr)
                    {
                        continue;
                    }

                    ve::EditorUiDrawCommand command;
                    command.elementCount = sourceCommand.ElemCount;
                    command.indexOffset = sourceCommand.IdxOffset;
                    command.vertexOffset = sourceCommand.VtxOffset;
                    command.clipRect[0] = sourceCommand.ClipRect.x;
                    command.clipRect[1] = sourceCommand.ClipRect.y;
                    command.clipRect[2] = sourceCommand.ClipRect.z;
                    command.clipRect[3] = sourceCommand.ClipRect.w;
                    command.textureId = static_cast<ve::UInt64>(sourceCommand.GetTexID());
                    drawList.commands.push_back(command);
                }

                frameData.drawLists.push_back(std::move(drawList));
            }

            return frameData;
        }

        void DrawEditor(HWND owner,
                        ve::Window& window,
                        ve::EditorProjectService& projectService,
                        ve::EngineRuntime& runtime)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);

            constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                     ImGuiWindowFlags_NoSavedSettings |
                                                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                                                     ImGuiWindowFlags_MenuBar;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
            ImGui::Begin("VEngineEditorMain", nullptr, windowFlags);

            DrawEditorMenu(owner, window, projectService, runtime);
            DrawEditorToolbar(projectService, runtime);

            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float statusHeight = 24.0f;
            const float assetHeight = std::min(std::max(available.y * 0.30f, 190.0f), 310.0f);
            const float topHeight = std::max(120.0f, available.y - assetHeight - statusHeight - spacing * 2.0f);
            const float hierarchyWidth = std::min(std::max(available.x * 0.20f, 240.0f), 360.0f);
            const float inspectorWidth = std::min(std::max(available.x * 0.24f, 300.0f), 430.0f);
            const float centerWidth = std::max(240.0f, available.x - hierarchyWidth - inspectorWidth - spacing * 2.0f);

            ImGui::BeginChild("SceneHierarchyPanel", ImVec2(hierarchyWidth, topHeight), true);
            editorPanels_.DrawSceneHierarchy(projectService, runtime, statusMessage_);
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("ViewportPanel", ImVec2(centerWidth, topHeight), true);
            if (ImGui::BeginTabBar("EditorViewTabs"))
            {
                if (ImGui::BeginTabItem("SceneView"))
                {
                    editorPanels_.DrawSceneView(projectService, runtime);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("GameView"))
                {
                    editorPanels_.DrawGameView(projectService, runtime);
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("InspectorPanel", ImVec2(0.0f, topHeight), true);
            editorPanels_.DrawInspector(projectService, runtime, statusMessage_);
            ImGui::EndChild();

            ImGui::BeginChild("AssetBrowserPanel", ImVec2(0.0f, assetHeight), true);
            editorPanels_.DrawAssetBrowser(projectService,
                                           runtime,
                                           statusMessage_,
                                           [this, &projectService, &runtime](const ve::Path& scenePath)
                                           { RequestOpenScene(projectService, runtime, scenePath); });
            ImGui::EndChild();

            DrawStatusBar(projectService);
            DrawDirtySceneModal(window, projectService, runtime);

            ImGui::End();
            ImGui::PopStyleVar();
        }

        void DrawEditorToolbar(ve::EditorProjectService& projectService, ve::EngineRuntime& runtime)
        {
            const bool playing = projectService.IsPlaying();

            ImGui::BeginDisabled(playing);
            if (ImGui::Button("Play"))
            {
                const ve::ErrorCode playResult = projectService.StartPlayMode(runtime.GetResourceManager());
                if (playResult == ve::ErrorCode::None)
                {
                    statusMessage_ = "Play mode started.";
                }
                else
                {
                    statusMessage_ = MakeProjectOpenError("Play failed", playResult);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(!playing);
            if (ImGui::Button("Stop"))
            {
                projectService.StopPlayMode();
                editorPanels_.ResetSelection();
                statusMessage_ = "Play mode stopped.";
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::TextDisabled("%s", playing ? "Playing" : "Editing");
            ImGui::Separator();
        }

        void DrawEditorMenu(HWND owner,
                            ve::Window& window,
                            ve::EditorProjectService& projectService,
                            ve::EngineRuntime& runtime)
        {
            if (!ImGui::BeginMenuBar())
            {
                return;
            }

            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open Project..."))
                {
                    ve::Result<ve::Path> selectedFolder =
                        ve::BrowseForWindowsProjectFolder(owner, L"Open VEngine project");
                    if (selectedFolder)
                    {
                        ve::WindowsProjectLauncherResult selection;
                        selection.accepted = true;
                        selection.projectRoot = selectedFolder.GetValue();

                        if (projectService.IsDirty())
                        {
                            RequestProjectOpen(std::move(selection), false);
                        }
                        else
                        {
                            (void)TryOpenProjectSelection(
                                *this, projectService, runtime, window, selection, false);
                        }
                    }
                }

                if (ImGui::MenuItem("Save Scene"))
                {
                    SaveCurrentScene(projectService, runtime);
                }

                if (ImGui::BeginMenu("Package Project"))
                {
                    const ve::PackageConfiguration configuration = GetEditorPackageConfiguration();
                    const std::string label = std::string("Windows ") + ve::ToString(configuration);
                    if (ImGui::MenuItem(label.c_str()))
                    {
                        PackageCurrentProject(projectService);
                    }

                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        void RequestOpenScene(ve::EditorProjectService& projectService,
                              ve::EngineRuntime& runtime,
                              const ve::Path& scenePath)
        {
            if (projectService.IsDirty())
            {
                pendingAction_ = {};
                pendingAction_.kind = PendingActionKind::OpenScene;
                pendingAction_.scenePath = scenePath;
                openDirtyScenePopup_ = true;
                return;
            }

            OpenScene(projectService, runtime, scenePath);
        }

        void OpenScene(ve::EditorProjectService& projectService,
                       ve::EngineRuntime& runtime,
                       const ve::Path& scenePath)
        {
            projectService.StopPlayMode();
            runtime.GetGameThreadSystem().ClearActiveScene();
            const ve::ErrorCode openResult = projectService.OpenScene(scenePath, runtime.GetResourceManager());
            if (openResult == ve::ErrorCode::None)
            {
                editorPanels_.ResetSelection();
                statusMessage_ = "Opened scene: " + scenePath.GetString();
                return;
            }

            statusMessage_ = MakeProjectOpenError("Open Scene failed", openResult);
        }

        void DrawStatusBar(const ve::EditorProjectService& projectService)
        {
            ImGui::Separator();
            const std::string sceneText = projectService.HasCurrentScene()
                                              ? projectService.GetCurrentScenePath().GetString()
                                              : std::string("No scene");
            ImGui::TextDisabled("%s | %s%s",
                                projectService.GetDescriptor().displayName.c_str(),
                                sceneText.c_str(),
                                projectService.IsDirty() ? " *" : "");
            if (projectService.IsPlaying())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("| Playing");
            }
            if (!statusMessage_.empty())
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(statusMessage_.c_str());
            }
        }

        void DrawDirtySceneModal(ve::Window& window,
                                 ve::EditorProjectService& projectService,
                                 ve::EngineRuntime& runtime)
        {
            if (openDirtyScenePopup_)
            {
                ImGui::OpenPopup("Unsaved Scene Changes");
                openDirtyScenePopup_ = false;
            }

            if (!ImGui::BeginPopupModal("Unsaved Scene Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                return;
            }

            ImGui::TextUnformatted("The current scene has unsaved changes.");
            ImGui::TextUnformatted("Save changes before continuing?");
            ImGui::Spacing();

            if (ImGui::Button("Save", ImVec2(96.0f, 0.0f)))
            {
                SaveCurrentScene(projectService, runtime);
                if (!projectService.IsDirty())
                {
                    PerformPendingAction(window, projectService, runtime);
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(96.0f, 0.0f)))
            {
                projectService.ClearDirty();
                PerformPendingAction(window, projectService, runtime);
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f)))
            {
                pendingAction_ = {};
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        void PerformPendingAction(ve::Window& window,
                                  ve::EditorProjectService& projectService,
                                  ve::EngineRuntime& runtime)
        {
            PendingAction action = std::move(pendingAction_);
            pendingAction_ = {};

            switch (action.kind)
            {
            case PendingActionKind::OpenScene:
                OpenScene(projectService, runtime, action.scenePath);
                break;
            case PendingActionKind::OpenProject:
                (void)TryOpenProjectSelection(*this,
                                              projectService,
                                              runtime,
                                              window,
                                              action.projectSelection,
                                              action.reportInLauncher);
                editorPanels_.ResetSelection();
                break;
            case PendingActionKind::None:
            default:
                break;
            }
        }

        [[nodiscard]] std::optional<ve::WindowsProjectLauncherResult> DrawLauncher(HWND owner)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);

            constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                     ImGuiWindowFlags_NoSavedSettings |
                                                     ImGuiWindowFlags_NoBringToFrontOnFocus;
            ImGui::Begin("VEngineProjectLauncher", nullptr, windowFlags);

            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float contentWidth = std::min(980.0f, std::max(availableWidth, 320.0f));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max((availableWidth - contentWidth) * 0.5f, 0.0f));

            std::optional<ve::WindowsProjectLauncherResult> result;
            ImGui::BeginChild("LauncherContent", ImVec2(contentWidth, 0.0f), false);
            ImGui::TextUnformatted("VEngine Editor");
            ImGui::SameLine();
            ImGui::TextDisabled("Project Launcher");
            ImGui::Separator();

            if (!pendingError_.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.38f, 0.32f, 1.0f));
                ImGui::TextWrapped("%s", pendingError_.c_str());
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }

            if (ImGui::BeginTable("LauncherTable", 2, ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 0.0f)))
            {
                ImGui::TableSetupColumn("Recent", ImGuiTableColumnFlags_WidthStretch, 0.66f);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 0.34f);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                ImGui::TextUnformatted("Recent Projects");
                ImGui::Spacing();
                if (recentProjects_.empty())
                {
                    ImGui::TextDisabled("No recent projects");
                }

                for (size_t index = 0; index < recentProjects_.size(); ++index)
                {
                    const ve::WindowsRecentProject& recentProject = recentProjects_[index];
                    const std::string title = MakeRecentProjectTitle(recentProject);
                    ImGui::PushID(static_cast<int>(index));
                    if (!recentProject.available)
                    {
                        ImGui::BeginDisabled();
                    }

                    if (ImGui::Selectable(title.c_str(), false, 0, ImVec2(0.0f, 48.0f)))
                    {
                        ve::WindowsProjectLauncherResult selection;
                        selection.accepted = true;
                        selection.projectRoot = recentProject.path;
                        result = std::move(selection);
                    }

                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", recentProject.path.GetString().c_str());
                    }

                    if (!recentProject.available)
                    {
                        ImGui::EndDisabled();
                    }

                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", recentProject.available ? "" : "missing");
                    ImGui::TextWrapped("%s", recentProject.path.GetString().c_str());
                    ImGui::Separator();
                    ImGui::PopID();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted("Actions");
                ImGui::Spacing();

                if (ImGui::Button("Browse...", ImVec2(-1.0f, 34.0f)))
                {
                    ve::Result<ve::Path> folder =
                        ve::BrowseForWindowsProjectFolder(owner, L"Open VEngine project");
                    if (folder)
                    {
                        ve::WindowsProjectLauncherResult selection;
                        selection.accepted = true;
                        selection.projectRoot = folder.GetValue();
                        result = std::move(selection);
                    }
                }

                if (ImGui::Button("Create Project...", ImVec2(-1.0f, 34.0f)))
                {
                    ve::Result<ve::Path> folder =
                        ve::BrowseForWindowsProjectFolder(owner, L"Select or create a new VEngine project folder");
                    if (folder)
                    {
                        ve::WindowsProjectLauncherResult selection;
                        selection.accepted = true;
                        selection.createProject = true;
                        selection.projectRoot = folder.GetValue();
                        result = std::move(selection);
                    }
                }

                if (ImGui::Button("Refresh", ImVec2(-1.0f, 34.0f)))
                {
                    RefreshRecentProjects();
                }

                ImGui::EndTable();
            }

            ImGui::EndChild();
            ImGui::End();
            return result;
        }

        HWND window_ = nullptr;
        std::vector<ve::WindowsRecentProject> recentProjects_;
        std::string pendingError_;
        std::string lastError_;
        std::string statusMessage_;
        ve::WindowsEditorPanels editorPanels_;
        PendingAction pendingAction_;
        bool initialized_ = false;
        bool fontAtlasSubmitted_ = false;
        bool openDirtyScenePopup_ = false;
        ve::EditorUiFontAtlas fontAtlas_;
    };

    [[nodiscard]] ve::ErrorCode EnsureLauncherUi(WindowsProjectLauncherUi& launcherUi, HWND window)
    {
        ve::ErrorCode initResult = launcherUi.Initialize(window);
        if (initResult != ve::ErrorCode::None)
        {
            const std::string message = launcherUi.GetLastError().empty()
                                            ? std::string("Project Launcher initialization failed.")
                                            : launcherUi.GetLastError();
            VE_LOG_ERROR_CATEGORY("Editor", "{}", message);
        }

        return initResult;
    }

    [[nodiscard]] bool TryOpenProjectSelection(WindowsProjectLauncherUi& launcherUi,
                                               ve::EditorProjectService& projectService,
                                               ve::EngineRuntime& runtime,
                                               ve::Window& window,
                                               const ve::WindowsProjectLauncherResult& selection,
                                               bool reportInLauncher)
    {
        if (!selection.accepted || selection.projectRoot.IsEmpty())
        {
            return false;
        }

        if (selection.createProject)
        {
            const ve::ErrorCode createResult =
                ve::EditorProjectService::CreateProjectSkeleton(selection.projectRoot,
                                                                MakeProjectDisplayName(selection.projectRoot));
            if (createResult != ve::ErrorCode::None)
            {
                const std::string message = MakeProjectOpenError("Failed to create VEngine project", createResult);
                VE_LOG_ERROR_CATEGORY("Editor", "{}", message);
                if (reportInLauncher)
                {
                    launcherUi.SetError(message);
                }
                else
                {
                    ShowErrorMessage(static_cast<HWND>(window.GetNativeHandle()), message);
                }
                return false;
            }
        }

        const ve::ErrorCode openResult = OpenEditorProject(projectService, runtime, selection.projectRoot, true);
        if (openResult == ve::ErrorCode::None)
        {
            launcherUi.ResetEditorSelection();
            launcherUi.SetStatus("Project open complete: " + projectService.GetDescriptor().displayName);
            return true;
        }

        const std::string message = MakeProjectOpenError("Failed to open VEngine project", openResult);
        VE_LOG_ERROR_CATEGORY("Editor", "{}", message);
        projectService.CloseProject(&runtime.GetGameThreadSystem());

        (void)EnsureLauncherUi(launcherUi, static_cast<HWND>(window.GetNativeHandle()));
        launcherUi.SetError(message);
        return false;
    }
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previousInstance, PWSTR commandLine, int showCommand)
{
    (void)instance;
    (void)previousInstance;
    (void)showCommand;

    ve::InitializeWin32DebugConsole(true);

    ve::ErrorCode loggingResult = ve::InitializeLogging();
    if (loggingResult != ve::ErrorCode::None)
    {
        return 1;
    }

    const WindowsComScope comScope;
    if (!comScope.IsAvailable())
    {
        ShowErrorMessage(nullptr, "VEngineEditor failed to initialize Windows shell services.");
        ve::ShutdownLogging();
        return 1;
    }

    ve::Path projectRoot = ParseProjectArgument(commandLine);
    ve::EditorProjectService projectService;
    WindowsProjectLauncherUi launcherUi;

    ve::ApplicationDesc desc;
    desc.name = "VEngineEditor";
    desc.mainWindow.title = "VEngine Editor";
    desc.mainWindow.width = 1600;
    desc.mainWindow.height = 900;
    desc.mainWindow.visible = true;
    desc.projectRoot = projectRoot;
    desc.initializeRenderingOnStartup = true;
    desc.runtime.jobSystem.workerThreadNamePrefix = "VEngineEditorJobWorker";
    desc.runtime.ioSystem.threadName = "VEngineEditorIOThread";
    desc.runtime.renderSystem.threadName = "VEngineEditorRenderThread";
    desc.runtime.gameThreadSystem.threadName = "VEngineEditorGameThread";

    desc.windowConfigure = [&launcherUi](ve::Window& window, ve::EngineRuntime&)
    {
        auto* win32Window = dynamic_cast<ve::Win32Window*>(&window);
        if (win32Window == nullptr)
        {
            return;
        }

        win32Window->SetNativeMessageHandler(
            [&launcherUi](HWND nativeWindow, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
            { return launcherUi.HandleWin32Message(nativeWindow, message, wParam, lParam, result); });
    };

    desc.sceneStartup = [&projectService, projectRoot](ve::EngineRuntime& runtime)
    {
        if (projectRoot.IsEmpty())
        {
            VE_LOG_INFO_CATEGORY("Editor", "No project argument supplied. Opening Project Launcher.");
            return ve::ErrorCode::None;
        }

        const ve::ErrorCode openResult = OpenEditorProject(projectService, runtime, projectRoot, true);
        if (openResult != ve::ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY("Editor", "Failed to open project: {}", ve::ToString(openResult));
        }

        return openResult;
    };

    desc.sceneShutdown = [&projectService, &launcherUi](ve::EngineRuntime& runtime)
    {
        launcherUi.Shutdown();
        projectService.CloseProject(&runtime.GetGameThreadSystem());
    };

    desc.frameUpdate = [&projectService, &launcherUi](ve::Window& window, ve::EngineRuntime& runtime)
    {
        HWND nativeWindow = static_cast<HWND>(window.GetNativeHandle());
        if (nativeWindow == nullptr)
        {
            return;
        }

        if (EnsureLauncherUi(launcherUi, nativeWindow) != ve::ErrorCode::None)
        {
            return;
        }

        if (projectService.HasOpenProject())
        {
            launcherUi.RenderEditor(nativeWindow, window, projectService, runtime);
            return;
        }

        std::optional<ve::WindowsProjectLauncherResult> selection =
            launcherUi.Render(nativeWindow, runtime.GetRenderSystem());
        if (selection)
        {
            (void)TryOpenProjectSelection(launcherUi, projectService, runtime, window, *selection, true);
        }
    };

    ve::Application application(std::move(desc));
    const int exitCode = application.Run();
    ve::ShutdownLogging();
    return exitCode;
}
