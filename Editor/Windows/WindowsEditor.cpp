#include "Editor/Core/EditorProject.h"
#include "Editor/Windows/WindowsProjectLauncher.h"

#include "Engine/Runtime/Application/Application.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Platform/Windows/Win32DebugConsole.h"
#include "Engine/Runtime/Platform/Windows/Win32Window.h"

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
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <memory>
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
    using Microsoft::WRL::ComPtr;

    constexpr std::string_view OpenProjectCommand = "Editor.OpenProject";

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

    [[nodiscard]] std::string MakeHResultError(const char* operation, HRESULT result)
    {
        char buffer[160] = {};
        std::snprintf(buffer,
                      sizeof(buffer),
                      "%s failed with HRESULT 0x%08X",
                      operation,
                      static_cast<unsigned>(result));
        return buffer;
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

    [[nodiscard]] ve::ErrorCode InitializeEditorRendering(ve::EngineRuntime& runtime,
                                                          ve::Window& window,
                                                          const ve::RenderDeviceDesc& deviceDesc)
    {
        ve::RenderSystem& renderSystem = runtime.GetRenderSystem();
        if (renderSystem.HasDevice())
        {
            return ve::ErrorCode::None;
        }

        ve::ErrorCode deviceResult = renderSystem.InitializeDevice(deviceDesc);
        if (deviceResult != ve::ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY("Editor", "Failed to initialize render device: {}", ve::ToString(deviceResult));
            return deviceResult;
        }

        const ve::WindowExtent extent = window.GetClientExtent();
        ve::RenderSurfaceDesc surfaceDesc;
        surfaceDesc.nativeWindow = window.GetNativeHandle();
        surfaceDesc.nativeLayer = window.GetNativeLayer();
        surfaceDesc.width = std::max(extent.width, 1u);
        surfaceDesc.height = std::max(extent.height, 1u);

        ve::ErrorCode swapchainResult = renderSystem.CreateMainSwapchain(surfaceDesc);
        if (swapchainResult != ve::ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY("Editor", "Failed to create render swapchain: {}", ve::ToString(swapchainResult));
            renderSystem.ShutdownDevice();
            return swapchainResult;
        }

        ve::GameThreadSystem& gameThreadSystem = runtime.GetGameThreadSystem();
        ve::ErrorCode renderConnectionResult = gameThreadSystem.SetRenderSystem(&renderSystem);
        if (renderConnectionResult != ve::ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY("Editor",
                                  "Failed to connect Game Thread to RenderSystem: {}",
                                  ve::ToString(renderConnectionResult));
            renderSystem.DestroyMainSwapchain();
            renderSystem.ShutdownDevice();
            return renderConnectionResult;
        }

        return ve::ErrorCode::None;
    }

    void ShutdownEditorRendering(ve::EngineRuntime& runtime) noexcept
    {
        ve::GameThreadSystem& gameThreadSystem = runtime.GetGameThreadSystem();
        gameThreadSystem.ClearActiveScene();
        gameThreadSystem.ClearRenderSystem();

        ve::RenderSystem& renderSystem = runtime.GetRenderSystem();
        renderSystem.DestroyMainSwapchain();
        renderSystem.ShutdownDevice();
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

        ve::ErrorCode bindResult = projectService.BindActiveScene(gameThreadSystem, runtime.GetResourceManager());
        if (bindResult != ve::ErrorCode::None)
        {
            return bindResult;
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
            io.IniFilename = nullptr;

            ImGui::StyleColorsDark();
            ApplyStyle();

            if (!CreateDeviceObjects(window_))
            {
                ImGui::DestroyContext();
                window_ = nullptr;
                return ve::ErrorCode::PlatformError;
            }

            if (!ImGui_ImplWin32_Init(window_))
            {
                DestroyDeviceObjects();
                ImGui::DestroyContext();
                window_ = nullptr;
                lastError_ = "ImGui Win32 backend initialization failed.";
                return ve::ErrorCode::PlatformError;
            }

            if (!ImGui_ImplDX11_Init(device_.Get(), context_.Get()))
            {
                ImGui_ImplWin32_Shutdown();
                DestroyDeviceObjects();
                ImGui::DestroyContext();
                window_ = nullptr;
                lastError_ = "ImGui D3D11 backend initialization failed.";
                return ve::ErrorCode::PlatformError;
            }

            initialized_ = true;
            return ve::ErrorCode::None;
        }

        void Shutdown() noexcept
        {
            if (initialized_)
            {
                ImGui_ImplDX11_Shutdown();
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();
            }

            DestroyDeviceObjects();
            initialized_ = false;
            window_ = nullptr;
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

        [[nodiscard]] std::optional<ve::WindowsProjectLauncherResult> Render(HWND owner)
        {
            if (!initialized_)
            {
                return std::nullopt;
            }

            ResizeIfNeeded();
            if (renderTargetView_ == nullptr || IsIconic(window_) != FALSE)
            {
                return std::nullopt;
            }

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            std::optional<ve::WindowsProjectLauncherResult> result = DrawLauncher(owner);

            ImGui::Render();
            const float clearColor[4] = {0.055f, 0.06f, 0.065f, 1.0f};
            ID3D11RenderTargetView* renderTargetView = renderTargetView_.Get();
            context_->OMSetRenderTargets(1, &renderTargetView, nullptr);
            context_->ClearRenderTargetView(renderTargetView_.Get(), clearColor);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            swapchain_->Present(1, 0);

            return result;
        }

        void RefreshRecentProjects()
        {
            recentProjects_ = ve::LoadWindowsRecentProjects();
        }

        void SetError(std::string message)
        {
            pendingError_ = std::move(message);
        }

        [[nodiscard]] const std::string& GetLastError() const noexcept
        {
            return lastError_;
        }

    private:
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

        [[nodiscard]] bool CreateDeviceObjects(HWND window)
        {
            RECT clientRect = {};
            GetClientRect(window, &clientRect);
            width_ = static_cast<UINT>(std::max<LONG>(clientRect.right - clientRect.left, 1));
            height_ = static_cast<UINT>(std::max<LONG>(clientRect.bottom - clientRect.top, 1));

            DXGI_SWAP_CHAIN_DESC swapchainDesc = {};
            swapchainDesc.BufferDesc.Width = width_;
            swapchainDesc.BufferDesc.Height = height_;
            swapchainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            swapchainDesc.BufferDesc.RefreshRate.Numerator = 60;
            swapchainDesc.BufferDesc.RefreshRate.Denominator = 1;
            swapchainDesc.SampleDesc.Count = 1;
            swapchainDesc.SampleDesc.Quality = 0;
            swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapchainDesc.BufferCount = 2;
            swapchainDesc.OutputWindow = window;
            swapchainDesc.Windowed = TRUE;
            swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

            UINT createDeviceFlags = 0;
#if defined(_DEBUG)
            createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

            const D3D_FEATURE_LEVEL featureLevels[] = {
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
            };
            D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

            HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr,
                                                           D3D_DRIVER_TYPE_HARDWARE,
                                                           nullptr,
                                                           createDeviceFlags,
                                                           featureLevels,
                                                           static_cast<UINT>(std::size(featureLevels)),
                                                           D3D11_SDK_VERSION,
                                                           &swapchainDesc,
                                                           &swapchain_,
                                                           &device_,
                                                           &featureLevel,
                                                           &context_);

#if defined(_DEBUG)
            if (FAILED(result))
            {
                createDeviceFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
                result = D3D11CreateDeviceAndSwapChain(nullptr,
                                                       D3D_DRIVER_TYPE_HARDWARE,
                                                       nullptr,
                                                       createDeviceFlags,
                                                       featureLevels,
                                                       static_cast<UINT>(std::size(featureLevels)),
                                                       D3D11_SDK_VERSION,
                                                       &swapchainDesc,
                                                       &swapchain_,
                                                       &device_,
                                                       &featureLevel,
                                                       &context_);
            }
#endif

            if (FAILED(result))
            {
                lastError_ = MakeHResultError("D3D11CreateDeviceAndSwapChain", result);
                return false;
            }

            return CreateRenderTarget();
        }

        void DestroyDeviceObjects() noexcept
        {
            CleanupRenderTarget();
            swapchain_.Reset();
            context_.Reset();
            device_.Reset();
            width_ = 0;
            height_ = 0;
        }

        [[nodiscard]] bool CreateRenderTarget()
        {
            ComPtr<ID3D11Texture2D> backBuffer;
            HRESULT result = swapchain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
            if (FAILED(result))
            {
                lastError_ = MakeHResultError("IDXGISwapChain::GetBuffer", result);
                return false;
            }

            result = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView_);
            if (FAILED(result))
            {
                lastError_ = MakeHResultError("ID3D11Device::CreateRenderTargetView", result);
                return false;
            }

            return true;
        }

        void CleanupRenderTarget() noexcept
        {
            renderTargetView_.Reset();
        }

        void ResizeIfNeeded()
        {
            RECT clientRect = {};
            GetClientRect(window_, &clientRect);
            const UINT width = static_cast<UINT>(std::max<LONG>(clientRect.right - clientRect.left, 1));
            const UINT height = static_cast<UINT>(std::max<LONG>(clientRect.bottom - clientRect.top, 1));
            if (width == width_ && height == height_)
            {
                return;
            }

            CleanupRenderTarget();
            HRESULT result = swapchain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(result))
            {
                lastError_ = MakeHResultError("IDXGISwapChain::ResizeBuffers", result);
                return;
            }

            width_ = width;
            height_ = height;
            (void)CreateRenderTarget();
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
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<IDXGISwapChain> swapchain_;
        ComPtr<ID3D11RenderTargetView> renderTargetView_;
        std::vector<ve::WindowsRecentProject> recentProjects_;
        std::string pendingError_;
        std::string lastError_;
        UINT width_ = 0;
        UINT height_ = 0;
        bool initialized_ = false;
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
                                               const ve::RenderDeviceDesc& renderDeviceDesc,
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

        const bool renderingWasInitialized = runtime.GetRenderSystem().HasDevice();
        if (!renderingWasInitialized)
        {
            launcherUi.Shutdown();
            const ve::ErrorCode renderResult = InitializeEditorRendering(runtime, window, renderDeviceDesc);
            if (renderResult != ve::ErrorCode::None)
            {
                const std::string message = MakeProjectOpenError("Failed to initialize Editor rendering", renderResult);
                (void)EnsureLauncherUi(launcherUi, static_cast<HWND>(window.GetNativeHandle()));
                launcherUi.SetError(message);
                return false;
            }
        }

        const ve::ErrorCode openResult = OpenEditorProject(projectService, runtime, selection.projectRoot, true);
        if (openResult == ve::ErrorCode::None)
        {
            launcherUi.Shutdown();
            return true;
        }

        const std::string message = MakeProjectOpenError("Failed to open VEngine project", openResult);
        VE_LOG_ERROR_CATEGORY("Editor", "{}", message);
        projectService.CloseProject(&runtime.GetGameThreadSystem());

        ShutdownEditorRendering(runtime);
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
    desc.mainWindow.menuItems.push_back({"File", "Open Project...", std::string(OpenProjectCommand)});
    desc.projectRoot = projectRoot;
    desc.initializeRenderingOnStartup = !projectRoot.IsEmpty();
    desc.runtime.jobSystem.workerThreadNamePrefix = "VEngineEditorJobWorker";
    desc.runtime.ioSystem.threadName = "VEngineEditorIOThread";
    desc.runtime.renderSystem.threadName = "VEngineEditorRenderThread";
    desc.runtime.gameThreadSystem.threadName = "VEngineEditorGameThread";

    const ve::RenderDeviceDesc renderDeviceDesc = desc.runtime.renderSystem.device;

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

    desc.frameUpdate = [&projectService, &launcherUi, renderDeviceDesc](ve::Window& window,
                                                                        ve::EngineRuntime& runtime)
    {
        if (projectService.HasOpenProject())
        {
            return;
        }

        HWND nativeWindow = static_cast<HWND>(window.GetNativeHandle());
        if (nativeWindow == nullptr)
        {
            return;
        }

        if (runtime.GetRenderSystem().HasDevice())
        {
            ShutdownEditorRendering(runtime);
        }

        if (EnsureLauncherUi(launcherUi, nativeWindow) != ve::ErrorCode::None)
        {
            return;
        }

        std::optional<ve::WindowsProjectLauncherResult> selection = launcherUi.Render(nativeWindow);
        if (selection)
        {
            (void)TryOpenProjectSelection(launcherUi,
                                          projectService,
                                          runtime,
                                          window,
                                          renderDeviceDesc,
                                          *selection,
                                          true);
        }
    };

    desc.commandHandler = [&projectService, &launcherUi, renderDeviceDesc](std::string_view command,
                                                                           ve::Window& window,
                                                                           ve::EngineRuntime& runtime)
    {
        if (command != OpenProjectCommand)
        {
            VE_LOG_INFO_CATEGORY("Editor", "Unhandled Editor command: {}", command);
            return;
        }

        ve::Result<ve::Path> selectedFolder =
            ve::BrowseForWindowsProjectFolder(static_cast<HWND>(window.GetNativeHandle()), L"Open VEngine project");
        if (!selectedFolder)
        {
            return;
        }

        ve::WindowsProjectLauncherResult selection;
        selection.accepted = true;
        selection.projectRoot = selectedFolder.GetValue();

        const bool reportInLauncher = !projectService.HasOpenProject();
        (void)TryOpenProjectSelection(launcherUi,
                                      projectService,
                                      runtime,
                                      window,
                                      renderDeviceDesc,
                                      selection,
                                      reportInLauncher);
    };

    ve::Application application(std::move(desc));
    const int exitCode = application.Run();
    ve::ShutdownLogging();
    return exitCode;
}
