#include "Editor/Core/Editor.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <imgui.h>

#if VE_PLATFORM_WINDOWS
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <d3d11.h>
#include <Windows.h>
#endif

#include <memory>
#include <vector>

namespace ve::editor
{
    struct EditorFrameDrawData
    {
        ImDrawData drawData;
        ImVector<ImTextureData*> textureRefs;
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

#if VE_PLATFORM_WINDOWS
        [[nodiscard]] ImGuiKey WindowsKeyCodeToImGuiKey(UInt32 keyCode, UInt32 scanCode, bool isExtended) noexcept
        {
            if (keyCode == VK_RETURN && isExtended)
            {
                return ImGuiKey_KeypadEnter;
            }

            switch (keyCode)
            {
            case VK_TAB:
                return ImGuiKey_Tab;
            case VK_LEFT:
                return ImGuiKey_LeftArrow;
            case VK_RIGHT:
                return ImGuiKey_RightArrow;
            case VK_UP:
                return ImGuiKey_UpArrow;
            case VK_DOWN:
                return ImGuiKey_DownArrow;
            case VK_PRIOR:
                return ImGuiKey_PageUp;
            case VK_NEXT:
                return ImGuiKey_PageDown;
            case VK_HOME:
                return ImGuiKey_Home;
            case VK_END:
                return ImGuiKey_End;
            case VK_INSERT:
                return ImGuiKey_Insert;
            case VK_DELETE:
                return ImGuiKey_Delete;
            case VK_BACK:
                return ImGuiKey_Backspace;
            case VK_SPACE:
                return ImGuiKey_Space;
            case VK_RETURN:
                return ImGuiKey_Enter;
            case VK_ESCAPE:
                return ImGuiKey_Escape;
            case VK_OEM_COMMA:
                return ImGuiKey_Comma;
            case VK_OEM_PERIOD:
                return ImGuiKey_Period;
            case VK_CAPITAL:
                return ImGuiKey_CapsLock;
            case VK_SCROLL:
                return ImGuiKey_ScrollLock;
            case VK_NUMLOCK:
                return ImGuiKey_NumLock;
            case VK_SNAPSHOT:
                return ImGuiKey_PrintScreen;
            case VK_PAUSE:
                return ImGuiKey_Pause;
            case VK_NUMPAD0:
                return ImGuiKey_Keypad0;
            case VK_NUMPAD1:
                return ImGuiKey_Keypad1;
            case VK_NUMPAD2:
                return ImGuiKey_Keypad2;
            case VK_NUMPAD3:
                return ImGuiKey_Keypad3;
            case VK_NUMPAD4:
                return ImGuiKey_Keypad4;
            case VK_NUMPAD5:
                return ImGuiKey_Keypad5;
            case VK_NUMPAD6:
                return ImGuiKey_Keypad6;
            case VK_NUMPAD7:
                return ImGuiKey_Keypad7;
            case VK_NUMPAD8:
                return ImGuiKey_Keypad8;
            case VK_NUMPAD9:
                return ImGuiKey_Keypad9;
            case VK_DECIMAL:
                return ImGuiKey_KeypadDecimal;
            case VK_DIVIDE:
                return ImGuiKey_KeypadDivide;
            case VK_MULTIPLY:
                return ImGuiKey_KeypadMultiply;
            case VK_SUBTRACT:
                return ImGuiKey_KeypadSubtract;
            case VK_ADD:
                return ImGuiKey_KeypadAdd;
            case VK_LSHIFT:
                return ImGuiKey_LeftShift;
            case VK_LCONTROL:
                return ImGuiKey_LeftCtrl;
            case VK_LMENU:
                return ImGuiKey_LeftAlt;
            case VK_LWIN:
                return ImGuiKey_LeftSuper;
            case VK_RSHIFT:
                return ImGuiKey_RightShift;
            case VK_RCONTROL:
                return ImGuiKey_RightCtrl;
            case VK_RMENU:
                return ImGuiKey_RightAlt;
            case VK_RWIN:
                return ImGuiKey_RightSuper;
            case VK_APPS:
                return ImGuiKey_Menu;
            case VK_BROWSER_BACK:
                return ImGuiKey_AppBack;
            case VK_BROWSER_FORWARD:
                return ImGuiKey_AppForward;
            default:
                break;
            }

            if (keyCode >= '0' && keyCode <= '9')
            {
                return static_cast<ImGuiKey>(ImGuiKey_0 + (keyCode - '0'));
            }

            if (keyCode >= 'A' && keyCode <= 'Z')
            {
                return static_cast<ImGuiKey>(ImGuiKey_A + (keyCode - 'A'));
            }

            if (keyCode >= VK_F1 && keyCode <= VK_F24)
            {
                return static_cast<ImGuiKey>(ImGuiKey_F1 + (keyCode - VK_F1));
            }

            switch (scanCode)
            {
            case 41:
                return ImGuiKey_GraveAccent;
            case 12:
                return ImGuiKey_Minus;
            case 13:
                return ImGuiKey_Equal;
            case 26:
                return ImGuiKey_LeftBracket;
            case 27:
                return ImGuiKey_RightBracket;
            case 86:
                return ImGuiKey_Oem102;
            case 43:
                return ImGuiKey_Backslash;
            case 39:
                return ImGuiKey_Semicolon;
            case 40:
                return ImGuiKey_Apostrophe;
            case 51:
                return ImGuiKey_Comma;
            case 52:
                return ImGuiKey_Period;
            case 53:
                return ImGuiKey_Slash;
            default:
                break;
            }

            return ImGuiKey_None;
        }
#endif

        [[nodiscard]] int MouseButtonToImGuiButton(InputMouseButton button) noexcept
        {
            switch (button)
            {
            case InputMouseButton::Left:
                return 0;
            case InputMouseButton::Right:
                return 1;
            case InputMouseButton::Middle:
                return 2;
            case InputMouseButton::X1:
                return 3;
            case InputMouseButton::X2:
                return 4;
            case InputMouseButton::Count:
                break;
            }

            return 0;
        }

        void SubmitModifierEvents(ImGuiIO& io, InputModifierFlags modifiers)
        {
            io.AddKeyEvent(ImGuiMod_Ctrl, HasInputModifier(modifiers, InputModifierFlags::Control));
            io.AddKeyEvent(ImGuiMod_Shift, HasInputModifier(modifiers, InputModifierFlags::Shift));
            io.AddKeyEvent(ImGuiMod_Alt, HasInputModifier(modifiers, InputModifierFlags::Alt));
            io.AddKeyEvent(ImGuiMod_Super, HasInputModifier(modifiers, InputModifierFlags::Super));
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

        auto ok = ImGui_ImplWin32_Init(nativeWindowHandle);
        VE_ASSERT(ok);

        renderSystem_ = &runtime.GetRenderSystem();
        const ErrorCode renderBackendResult = InitRenderBackend(*renderSystem_);
        VE_ASSERT(renderBackendResult == ErrorCode::None);

        sceneSystem_ = &runtime.GetSceneSystem();
        initialized_.store(true, std::memory_order_release);
        sceneSystem_->SetEditorCallback(SceneSystemEditorCallback{
            .onStartFrame = [this]() { StartFrame(); },
            .onOSEvent = [this](const OSEvent& event) { return OnOSEvent(event); },
            .onRender = [this]() { Render(); },
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

    bool Editor::OnOSEvent(const OSEvent& event)
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_.load(std::memory_order_acquire))
        {
            return false;
        }

        ImGuiIO& io = ImGui::GetIO();

        switch (event.type)
        {
        case OSEventType::WindowFocusGained:
            io.AddFocusEvent(true);
            return true;
        case OSEventType::WindowFocusLost:
            io.AddFocusEvent(false);
            return true;
        case OSEventType::KeyboardKeyDown:
        case OSEventType::KeyboardKeyUp:
        {
            SubmitModifierEvents(io, event.modifiers);
#if VE_PLATFORM_WINDOWS
            const ImGuiKey imguiKey = WindowsKeyCodeToImGuiKey(event.keyCode, event.scanCode, event.isExtended);
            if (imguiKey != ImGuiKey_None)
            {
                io.AddKeyEvent(imguiKey, event.type == OSEventType::KeyboardKeyDown);
            }
#endif
            return false;
        }
        case OSEventType::TextInput:
            io.AddInputCharacterUTF16(static_cast<ImWchar16>(event.textCodepoint));
            return false;
        case OSEventType::MouseMoved:
            io.AddMousePosEvent(static_cast<float>(event.mouseX), static_cast<float>(event.mouseY));
            return false;
        case OSEventType::MouseButtonDown:
        case OSEventType::MouseButtonUp:
            io.AddMousePosEvent(static_cast<float>(event.mouseX), static_cast<float>(event.mouseY));
            io.AddMouseButtonEvent(MouseButtonToImGuiButton(event.mouseButton),
                                   event.type == OSEventType::MouseButtonDown);
            return false;
        case OSEventType::MouseWheel:
            io.AddMousePosEvent(static_cast<float>(event.mouseX), static_cast<float>(event.mouseY));
            io.AddMouseWheelEvent(event.mouseWheelX, event.mouseWheelY);
            return false;
        case OSEventType::WindowMinimized:
        case OSEventType::WindowRestored:
        case OSEventType::WindowResized:
        case OSEventType::WindowShown:
        case OSEventType::WindowHidden:
        case OSEventType::FrameEndFenceSignal:
            return false;
        }

        return false;
    }

    void Editor::Render()
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_.load(std::memory_order_acquire))
        {
            return;
        }

        if (showDemoWindow_)
        {
            ImGui::ShowDemoWindow(&showDemoWindow_);
        }
        ImGui::Render();

        std::shared_ptr<EditorFrameDrawData> frameDrawData = CloneFrameDrawData(ImGui::GetDrawData());
        VE_ASSERT_MESSAGE(frameDrawData != nullptr, "Editor::Render requires valid ImGui draw data.");

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

        ErrorCode submitResult = renderSystem_->EnqueueCommand(RenderCommand{"RenderEditor", std::move(lambda)});
        VE_ASSERT_MESSAGE(submitResult == ErrorCode::None, "Editor::Render failed to submit render command.");
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
