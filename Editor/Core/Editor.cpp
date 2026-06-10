#include "Editor/Core/Editor.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <imgui.h>

#if VE_PLATFORM_WINDOWS
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <d3d11.h>
#endif

#include <memory>
#include <vector>

namespace ve::editor
{
    struct EditorFrameDrawData
    {
        ImDrawData drawData;
        std::vector<std::unique_ptr<ImDrawList>> ownedCmdLists;
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

    ErrorCode Editor::Init(EngineRuntime& runtime, void* nativeWindowHandle)
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

#if VE_PLATFORM_WINDOWS
        if (!ImGui_ImplWin32_Init(nativeWindowHandle))
        {
            ImGui::DestroyContext();
            return ErrorCode::PlatformError;
        }
#else
        (void)nativeWindowHandle;
        ImGui::DestroyContext();
        return ErrorCode::Unsupported;
#endif
        renderSystem_ = &runtime.GetRenderSystem();
        const ErrorCode renderBackendResult = InitRenderBackend(*renderSystem_);
        VE_ASSERT(renderBackendResult == ErrorCode::None);

        sceneSystem_ = &runtime.GetSceneSystem();
        initialized_.store(true, std::memory_order_release);
        sceneSystem_->SetEditorCallback(SceneSystemEditorCallback{
            .onStartFrame = [this]() { StartFrame(); },
            .onOSEvent = [this](const OSEvent& event) { OnOSEvent(event); },
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

#if VE_PLATFORM_WINDOWS
        ImGui_ImplWin32_NewFrame();
#endif
        ImGui::NewFrame();
    }

    void Editor::OnOSEvent(const OSEvent& event)
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_.load(std::memory_order_acquire))
        {
            return;
        }

        // Temporarily ignore input/event dispatch.
        (void)event;
    }

    void Editor::Render()
    {
        VE_ASSERT_SCENE_THREAD();
        ImGui::ShowDemoWindow(&showDemoWindow_);
        ImGui::Render();

        std::shared_ptr<EditorFrameDrawData> frameDrawData = CloneFrameDrawData(ImGui::GetDrawData());
        VE_ASSERT(frameDrawData != nullptr);

        auto lambda = [this, frameDrawData = std::move(frameDrawData)]()
        {
            VE_ASSERT_RENDER_THREAD();
            if (!initialized_.load(std::memory_order_acquire))
            {
                return;
            }

            switch (renderBackend_)
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
        auto error = renderSystem_->Submit(RenderCommand("RenderEditor", lambda));
        VE_ASSERT(error == ErrorCode::None);
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
        ShutdownRenderBackend();

#if VE_PLATFORM_WINDOWS
        ImGui_ImplWin32_Shutdown();
#endif

        VE_ASSERT_MESSAGE(ImGui::GetCurrentContext() != nullptr, "Editor::UnInit requires an active ImGui context.");
        ImGui::DestroyContext();

        renderSystem_ = nullptr;
        VE_LOG_INFO_CATEGORY("Editor", "Editor uninitialized.");
    }

    bool Editor::IsInitialized() const noexcept
    {
        return initialized_.load(std::memory_order_acquire);
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
