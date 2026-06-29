#include "Editor/Core/Editor.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"

#include <backends/imgui_impl_metal.h>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace ve::editor
{
    namespace
    {
        constexpr NSUInteger ImGuiBackendFramebufferWidth = 1;
        constexpr NSUInteger ImGuiBackendFramebufferHeight = 1;

        [[nodiscard]] MTLTextureDescriptor* CreateImGuiFramebufferTextureDescriptor()
        {
            MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                                         width:ImGuiBackendFramebufferWidth
                                                                                                        height:ImGuiBackendFramebufferHeight
                                                                                                     mipmapped:NO];
            textureDescriptor.usage = MTLTextureUsageRenderTarget;
            textureDescriptor.storageMode = MTLStorageModePrivate;
            return textureDescriptor;
        }
    } // namespace

    void ApplyMacMainWindowTitle(void* nativeWindowHandle, const std::string& title)
    {
        NSView* view = static_cast<NSView*>(nativeWindowHandle);
        if (view == nil)
        {
            return;
        }

        [[view window] setTitle:[NSString stringWithUTF8String:title.c_str()]];
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

        renderBackendNativeDevice_ = nativeDevice;
        return ErrorCode::None;
    }

    void Editor::BeginRenderBackendFrame()
    {
        if (renderBackend_ != RenderBackend::Metal || renderBackendNativeDevice_ == nullptr)
        {
            return;
        }

        auto* nativeDevice = static_cast<id<MTLDevice>>(renderBackendNativeDevice_);
        MTLTextureDescriptor* textureDescriptor = CreateImGuiFramebufferTextureDescriptor();
        id<MTLTexture> texture = [nativeDevice newTextureWithDescriptor:textureDescriptor];
        if (texture == nil)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to allocate transient ImGui Metal framebuffer descriptor texture.");
            return;
        }

        MTLRenderPassDescriptor* renderPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        renderPassDescriptor.colorAttachments[0].texture = texture;
        renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
        renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        ImGui_ImplMetal_NewFrame(renderPassDescriptor);
        [texture release];
    }

    void Editor::ShutdownRenderBackend() noexcept
    {
        if (renderBackend_ != RenderBackend::Metal)
        {
            return;
        }

        VE_ASSERT_MESSAGE(ImGui::GetCurrentContext() != nullptr, "Editor::ShutdownRenderBackend requires an active ImGui context.");
        ImGui_ImplMetal_Shutdown();
        renderBackendNativeDevice_ = nullptr;
    }

    void Editor::RenderImGuiDrawData(RenderBackend backend, rhi::RhiCommandList& commandList, ImDrawData& drawData)
    {
        if (backend != RenderBackend::Metal)
        {
            return;
        }

        auto* commandBuffer = static_cast<id<MTLCommandBuffer>>(commandList.GetNativeCommandBufferHandle());
        auto* renderCommandEncoder = static_cast<id<MTLRenderCommandEncoder>>(commandList.GetNativeRenderEncoderHandle());
        if (commandBuffer == nil || renderCommandEncoder == nil)
        {
            return;
        }

        ImGui_ImplMetal_RenderDrawData(&drawData, commandBuffer, renderCommandEncoder);
    }
} // namespace ve::editor
