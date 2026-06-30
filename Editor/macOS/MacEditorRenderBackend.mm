#include "Editor/macOS/MacEditorRenderBackend.h"

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"

#include <backends/imgui_impl_metal.h>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <memory>
#include <string>

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

    ErrorCode MacEditorRenderBackend::Init(RenderSystem& renderSystem)
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
        if (nativeHandles.backend != RenderBackend::Metal)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Mac editor currently only supports Metal render backend.");
            return ErrorCode::Unsupported;
        }

        auto* nativeDevice = static_cast<id<MTLDevice>>(nativeHandles.device);
        if (nativeDevice == nil)
        {
            return ErrorCode::InvalidState;
        }

        if (!ImGui_ImplMetal_Init(nativeDevice))
        {
            return ErrorCode::PlatformError;
        }

        nativeDevice_ = nativeDevice;
        initialized_ = true;
        return ErrorCode::None;
    }

    void MacEditorRenderBackend::BeginFrame()
    {
        if (!initialized_ || backend_ != RenderBackend::Metal || nativeDevice_ == nullptr)
        {
            return;
        }

        @autoreleasepool
        {
            auto* nativeDevice = static_cast<id<MTLDevice>>(nativeDevice_);
            if (imguiFramebufferTexture_ == nullptr)
            {
                MTLTextureDescriptor* textureDescriptor = CreateImGuiFramebufferTextureDescriptor();
                id<MTLTexture> texture = [nativeDevice newTextureWithDescriptor:textureDescriptor];
                if (texture == nil)
                {
                    VE_LOG_WARN_CATEGORY("Editor", "Failed to allocate ImGui Metal framebuffer descriptor texture.");
                    return;
                }
                imguiFramebufferTexture_ = texture;
            }

            auto* texture = static_cast<id<MTLTexture>>(imguiFramebufferTexture_);
            MTLRenderPassDescriptor* renderPassDescriptor = [[MTLRenderPassDescriptor alloc] init];
            renderPassDescriptor.colorAttachments[0].texture = texture;
            renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
            renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
            ImGui_ImplMetal_NewFrame(renderPassDescriptor);
            [renderPassDescriptor release];
        }
    }

    void MacEditorRenderBackend::Shutdown() noexcept
    {
        if (!initialized_)
        {
            return;
        }

        VE_ASSERT_MESSAGE(ImGui::GetCurrentContext() != nullptr, "MacEditorRenderBackend::Shutdown requires an active ImGui context.");
        ImGui_ImplMetal_Shutdown();
        [static_cast<id<MTLTexture>>(imguiFramebufferTexture_) release];
        imguiFramebufferTexture_ = nullptr;
        nativeDevice_ = nullptr;
        initialized_ = false;
    }

    void MacEditorRenderBackend::RenderDrawData(rhi::RhiCommandList& commandList, ImDrawData& drawData)
    {
        if (!initialized_ || backend_ != RenderBackend::Metal)
        {
            return;
        }

        auto* commandBuffer = static_cast<id<MTLCommandBuffer>>(commandList.GetNativeCommandBufferHandle());
        auto* renderCommandEncoder = static_cast<id<MTLRenderCommandEncoder>>(commandList.GetNativeRenderEncoderHandle());
        if (commandBuffer == nil || renderCommandEncoder == nil)
        {
            return;
        }

        @autoreleasepool
        {
            ImGui_ImplMetal_RenderDrawData(&drawData, commandBuffer, renderCommandEncoder);
        }
    }

    std::unique_ptr<EditorRenderBackend> CreateMacEditorRenderBackend()
    {
        return std::make_unique<MacEditorRenderBackend>();
    }
} // namespace ve::editor
