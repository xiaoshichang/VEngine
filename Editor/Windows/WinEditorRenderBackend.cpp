#include "Editor/Windows/WinEditorRenderBackend.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"

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

#include <memory>

namespace ve::editor
{
    ErrorCode WinEditorRenderBackend::Init(RenderSystem& renderSystem)
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

        backend_ = nativeHandles.backend;
        if (nativeHandles.backend != RenderBackend::D3D11)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Windows editor currently supports ImGui rendering through the D3D11 backend.");
            return ErrorCode::Unsupported;
        }

        auto* nativeDevice = static_cast<ID3D11Device*>(nativeHandles.device);
        auto* nativeContext = static_cast<ID3D11DeviceContext*>(nativeHandles.immediateContext);
        if (nativeDevice == nullptr || nativeContext == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        if (!ImGui_ImplDX11_Init(nativeDevice, nativeContext))
        {
            return ErrorCode::PlatformError;
        }

        nativeDevice_ = nativeDevice;
        initialized_ = true;
        return ErrorCode::None;
    }

    void WinEditorRenderBackend::BeginFrame()
    {
        if (!initialized_ || backend_ != RenderBackend::D3D11)
        {
            return;
        }

        ImGui_ImplDX11_NewFrame();
    }

    void WinEditorRenderBackend::Shutdown() noexcept
    {
        if (!initialized_)
        {
            return;
        }

        VE_ASSERT_MESSAGE(ImGui::GetCurrentContext() != nullptr, "WinEditorRenderBackend::Shutdown requires an active ImGui context.");
        ImGui_ImplDX11_Shutdown();
        nativeDevice_ = nullptr;
        initialized_ = false;
    }

    void WinEditorRenderBackend::RenderDrawData(rhi::RhiCommandList& commandList, ImDrawData& drawData)
    {
        (void)commandList;

        if (!initialized_ || backend_ != RenderBackend::D3D11)
        {
            return;
        }

        ImGui_ImplDX11_RenderDrawData(&drawData);
    }

    std::unique_ptr<EditorRenderBackend> CreateWinEditorRenderBackend()
    {
        return std::make_unique<WinEditorRenderBackend>();
    }
} // namespace ve::editor
