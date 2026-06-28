#include "Editor/Core/Editor.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"

#include <backends/imgui_impl_metal.h>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace ve::editor
{
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
        if (nativeHandles.backend != RenderBackend::Metal)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Mac editor currently only supports Metal render backend.");
            return ErrorCode::Unsupported;
        }

        auto* nativeDevice = static_cast<id<MTLDevice>>(nativeHandles.device);
        if (nativeDevice == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        if (!ImGui_ImplMetal_Init(nativeDevice))
        {
            return ErrorCode::PlatformError;
        }

        return ErrorCode::None;
    }

    void Editor::ShutdownRenderBackend() noexcept
    {
        if (renderBackend_ != RenderBackend::Metal)
        {
            return;
        }

        VE_ASSERT_MESSAGE(ImGui::GetCurrentContext() != nullptr, "Editor::ShutdownRenderBackend requires an active ImGui context.");
        ImGui_ImplMetal_Shutdown();
    }
} // namespace ve::editor
