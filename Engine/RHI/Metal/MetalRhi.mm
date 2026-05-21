#include "Engine/RHI/Metal/MetalRhi.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <memory>
#include <string>
#include <utility>

namespace ve::rhi
{
namespace
{
MTLPixelFormat ToMetalPixelFormat(RhiFormat format)
{
    switch (format)
    {
    case RhiFormat::Rgba8Unorm:
        return MTLPixelFormatRGBA8Unorm;
    case RhiFormat::Bgra8Unorm:
        return MTLPixelFormatBGRA8Unorm;
    case RhiFormat::Rgb32Float:
    case RhiFormat::Unknown:
    default:
        return MTLPixelFormatInvalid;
    }
}

MTLVertexFormat ToMetalVertexFormat(RhiFormat format)
{
    switch (format)
    {
    case RhiFormat::Rgb32Float:
        return MTLVertexFormatFloat3;
    case RhiFormat::Rgba8Unorm:
    case RhiFormat::Bgra8Unorm:
    case RhiFormat::Unknown:
    default:
        return MTLVertexFormatInvalid;
    }
}

MTLPrimitiveType ToMetalPrimitiveType(RhiPrimitiveTopology topology)
{
    switch (topology)
    {
    case RhiPrimitiveTopology::TriangleList:
    default:
        return MTLPrimitiveTypeTriangle;
    }
}

std::string ToString(NSError* error)
{
    if (error == nil)
    {
        return "Unknown Metal error.";
    }

    return [[error localizedDescription] UTF8String];
}

class MetalBuffer final : public RhiBuffer
{
public:
    MetalBuffer(id<MTLBuffer> buffer, uint64_t size)
        : buffer_(buffer)
        , size_(size)
    {
    }

    [[nodiscard]] uint64_t GetSize() const noexcept override
    {
        return size_;
    }

    [[nodiscard]] id<MTLBuffer> GetNativeBuffer() const noexcept
    {
        return buffer_;
    }

private:
    id<MTLBuffer> buffer_ = nil;
    uint64_t size_ = 0;
};

class MetalShaderModule final : public RhiShaderModule
{
public:
    MetalShaderModule(RhiShaderStage stage, id<MTLLibrary> library, id<MTLFunction> function)
        : stage_(stage)
        , library_(library)
        , function_(function)
    {
    }

    [[nodiscard]] RhiShaderStage GetStage() const noexcept override
    {
        return stage_;
    }

    [[nodiscard]] id<MTLFunction> GetFunction() const noexcept
    {
        return function_;
    }

private:
    RhiShaderStage stage_ = RhiShaderStage::Vertex;
    id<MTLLibrary> library_ = nil;
    id<MTLFunction> function_ = nil;
};

class MetalPipelineState final : public RhiPipelineState
{
public:
    MetalPipelineState(RhiPrimitiveTopology topology, id<MTLRenderPipelineState> pipelineState)
        : topology_(topology)
        , pipelineState_(pipelineState)
    {
    }

    [[nodiscard]] RhiPrimitiveTopology GetTopology() const noexcept override
    {
        return topology_;
    }

    [[nodiscard]] id<MTLRenderPipelineState> GetNativePipelineState() const noexcept
    {
        return pipelineState_;
    }

private:
    RhiPrimitiveTopology topology_ = RhiPrimitiveTopology::TriangleList;
    id<MTLRenderPipelineState> pipelineState_ = nil;
};

class MetalSwapchain final : public RhiSwapchain
{
public:
    MetalSwapchain(CAMetalLayer* layer, id<MTLDevice> device, RhiExtent2D extent, RhiFormat colorFormat)
        : layer_(layer)
        , device_(device)
        , extent_(extent)
        , colorFormat_(colorFormat)
    {
    }

    [[nodiscard]] bool Initialize()
    {
        if (layer_ == nil || device_ == nil)
        {
            return false;
        }

        layer_.device = device_;
        layer_.pixelFormat = ToMetalPixelFormat(colorFormat_);
        layer_.framebufferOnly = YES;
        layer_.drawableSize = CGSizeMake(extent_.width, extent_.height);
        return true;
    }

    [[nodiscard]] RhiExtent2D GetExtent() const noexcept override
    {
        return extent_;
    }

    [[nodiscard]] RhiFormat GetColorFormat() const noexcept override
    {
        return colorFormat_;
    }

    [[nodiscard]] bool Present() override
    {
        return true;
    }

    [[nodiscard]] id<CAMetalDrawable> AcquireDrawable()
    {
        return [layer_ nextDrawable];
    }

private:
    CAMetalLayer* layer_ = nil;
    id<MTLDevice> device_ = nil;
    RhiExtent2D extent_ = {};
    RhiFormat colorFormat_ = RhiFormat::Bgra8Unorm;
};

class MetalCommandList final : public RhiCommandList
{
public:
    explicit MetalCommandList(id<MTLCommandQueue> commandQueue)
        : commandQueue_(commandQueue)
    {
    }

    [[nodiscard]] bool Begin() override
    {
        commandBuffer_ = [commandQueue_ commandBuffer];
        return commandBuffer_ != nil;
    }

    [[nodiscard]] bool End() override
    {
        return commandBuffer_ != nil;
    }

    [[nodiscard]] bool BeginRenderPass(RhiSwapchain& swapchain, const RhiRenderPassDesc& desc) override
    {
        auto* metalSwapchain = dynamic_cast<MetalSwapchain*>(&swapchain);

        if (metalSwapchain == nullptr || commandBuffer_ == nil)
        {
            return false;
        }

        drawable_ = [metalSwapchain->AcquireDrawable() retain];

        if (drawable_ == nil)
        {
            return false;
        }

        MTLRenderPassDescriptor* renderPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        renderPassDescriptor.colorAttachments[0].texture = drawable_.texture;
        renderPassDescriptor.colorAttachments[0].loadAction =
            desc.colorLoadAction == RhiLoadAction::Clear ? MTLLoadActionClear : MTLLoadActionLoad;
        renderPassDescriptor.colorAttachments[0].storeAction =
            desc.colorStoreAction == RhiStoreAction::Store ? MTLStoreActionStore : MTLStoreActionDontCare;
        renderPassDescriptor.colorAttachments[0].clearColor =
            MTLClearColorMake(desc.clearColor.r, desc.clearColor.g, desc.clearColor.b, desc.clearColor.a);

        renderCommandEncoder_ = [commandBuffer_ renderCommandEncoderWithDescriptor:renderPassDescriptor];
        return renderCommandEncoder_ != nil;
    }

    void EndRenderPass() override
    {
        [renderCommandEncoder_ endEncoding];
        renderCommandEncoder_ = nil;
    }

    void SetPipeline(const RhiPipelineState& pipelineState) override
    {
        const auto& metalPipelineState = static_cast<const MetalPipelineState&>(pipelineState);
        [renderCommandEncoder_ setRenderPipelineState:metalPipelineState.GetNativePipelineState()];
    }

    void SetViewport(const RhiViewport& viewport) override
    {
        MTLViewport metalViewport = {};
        metalViewport.originX = viewport.x;
        metalViewport.originY = viewport.y;
        metalViewport.width = viewport.width;
        metalViewport.height = viewport.height;
        metalViewport.znear = viewport.minDepth;
        metalViewport.zfar = viewport.maxDepth;
        [renderCommandEncoder_ setViewport:metalViewport];
    }

    void SetScissor(const RhiScissorRect& scissorRect) override
    {
        MTLScissorRect metalScissorRect = {};
        metalScissorRect.x = static_cast<NSUInteger>(scissorRect.x);
        metalScissorRect.y = static_cast<NSUInteger>(scissorRect.y);
        metalScissorRect.width = scissorRect.width;
        metalScissorRect.height = scissorRect.height;
        [renderCommandEncoder_ setScissorRect:metalScissorRect];
    }

    void SetVertexBuffer(uint32_t slot, const RhiBuffer& buffer, uint32_t stride, uint64_t offset) override
    {
        (void)stride;
        const auto& metalBuffer = static_cast<const MetalBuffer&>(buffer);
        [renderCommandEncoder_ setVertexBuffer:metalBuffer.GetNativeBuffer() offset:offset atIndex:slot];
    }

    void Draw(uint32_t vertexCount, uint32_t firstVertex) override
    {
        [renderCommandEncoder_ drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:firstVertex vertexCount:vertexCount];
    }

    [[nodiscard]] bool CommitAndWait()
    {
        if (commandBuffer_ == nil)
        {
            return false;
        }

        if (drawable_ != nil)
        {
            [commandBuffer_ presentDrawable:drawable_];
        }

        [commandBuffer_ commit];
        [commandBuffer_ waitUntilCompleted];

        [drawable_ release];
        drawable_ = nil;
        commandBuffer_ = nil;
        return true;
    }

private:
    id<MTLCommandQueue> commandQueue_ = nil;
    id<MTLCommandBuffer> commandBuffer_ = nil;
    id<MTLRenderCommandEncoder> renderCommandEncoder_ = nil;
    id<CAMetalDrawable> drawable_ = nil;
};

class MetalDevice final : public RhiDevice
{
public:
    explicit MetalDevice(bool enableDebug)
        : enableDebug_(enableDebug)
    {
    }

    [[nodiscard]] bool Initialize()
    {
        (void)enableDebug_;

        device_ = MTLCreateSystemDefaultDevice();

        if (device_ == nil)
        {
            SetLastError("MTLCreateSystemDefaultDevice returned nil.");
            return false;
        }

        commandQueue_ = [device_ newCommandQueue];

        if (commandQueue_ == nil)
        {
            SetLastError("MTLDevice newCommandQueue returned nil.");
            return false;
        }

        return true;
    }

    [[nodiscard]] RhiBackend GetBackend() const noexcept override
    {
        return RhiBackend::Metal;
    }

    [[nodiscard]] const char* GetLastErrorMessage() const noexcept override
    {
        return lastError_.c_str();
    }

    [[nodiscard]] std::unique_ptr<RhiSwapchain> CreateSwapchain(const RhiSwapchainDesc& desc) override
    {
        auto* layer = static_cast<CAMetalLayer*>(desc.nativeLayer);

        if (layer == nil)
        {
            SetLastError("Metal swapchain requires a CAMetalLayer nativeLayer.");
            return nullptr;
        }

        auto swapchain = std::make_unique<MetalSwapchain>(
            layer, device_, RhiExtent2D{desc.width, desc.height}, desc.colorFormat);

        if (!swapchain->Initialize())
        {
            SetLastError("Metal swapchain initialization failed.");
            return nullptr;
        }

        return swapchain;
    }

    [[nodiscard]] std::unique_ptr<RhiBuffer> CreateBuffer(const RhiBufferDesc& desc) override
    {
        id<MTLBuffer> buffer = nil;

        if (desc.initialData != nullptr)
        {
            buffer = [device_ newBufferWithBytes:desc.initialData length:desc.size options:MTLResourceStorageModeShared];
        }
        else
        {
            buffer = [device_ newBufferWithLength:desc.size options:MTLResourceStorageModeShared];
        }

        if (buffer == nil)
        {
            SetLastError("MTLDevice newBuffer failed.");
            return nullptr;
        }

        return std::make_unique<MetalBuffer>(buffer, desc.size);
    }

    [[nodiscard]] std::unique_ptr<RhiShaderModule> CreateShaderModule(const RhiShaderModuleDesc& desc) override
    {
        if (desc.source == nullptr || desc.entryPoint == nullptr)
        {
            SetLastError("Metal shader module requires source and entry point.");
            return nullptr;
        }

        NSString* source = [NSString stringWithUTF8String:desc.source];
        NSError* error = nil;
        id<MTLLibrary> library = [device_ newLibraryWithSource:source options:nil error:&error];

        if (library == nil)
        {
            SetLastError(ToString(error));
            return nullptr;
        }

        NSString* functionName = [NSString stringWithUTF8String:desc.entryPoint];
        id<MTLFunction> function = [library newFunctionWithName:functionName];

        if (function == nil)
        {
            SetLastError("MTLLibrary newFunctionWithName failed.");
            return nullptr;
        }

        return std::make_unique<MetalShaderModule>(desc.stage, library, function);
    }

    [[nodiscard]] std::unique_ptr<RhiPipelineState> CreateGraphicsPipeline(
        const RhiGraphicsPipelineDesc& desc) override
    {
        const auto* vertexShaderModule = dynamic_cast<const MetalShaderModule*>(desc.vertexShader);
        const auto* fragmentShaderModule = dynamic_cast<const MetalShaderModule*>(desc.fragmentShader);

        if (vertexShaderModule == nullptr || fragmentShaderModule == nullptr)
        {
            SetLastError("Metal graphics pipeline requires Metal shader modules.");
            return nullptr;
        }

        MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];

        for (uint32_t index = 0; index < desc.vertexLayout.attributeCount; ++index)
        {
            const RhiVertexAttributeDesc& attribute = desc.vertexLayout.attributes[index];
            vertexDescriptor.attributes[index].format = ToMetalVertexFormat(attribute.format);
            vertexDescriptor.attributes[index].offset = attribute.offset;
            vertexDescriptor.attributes[index].bufferIndex = 0;
        }

        vertexDescriptor.layouts[0].stride = desc.vertexLayout.stride;
        vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        vertexDescriptor.layouts[0].stepRate = 1;

        MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDescriptor.vertexFunction = vertexShaderModule->GetFunction();
        pipelineDescriptor.fragmentFunction = fragmentShaderModule->GetFunction();
        pipelineDescriptor.vertexDescriptor = vertexDescriptor;
        pipelineDescriptor.colorAttachments[0].pixelFormat = ToMetalPixelFormat(desc.colorFormat);

        NSError* error = nil;
        id<MTLRenderPipelineState> pipelineState =
            [device_ newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];

        [pipelineDescriptor release];
        [vertexDescriptor release];

        if (pipelineState == nil)
        {
            SetLastError(ToString(error));
            return nullptr;
        }

        return std::make_unique<MetalPipelineState>(desc.topology, pipelineState);
    }

    [[nodiscard]] std::unique_ptr<RhiCommandList> CreateCommandList() override
    {
        return std::make_unique<MetalCommandList>(commandQueue_);
    }

    [[nodiscard]] bool Submit(RhiCommandList& commandList) override
    {
        auto* metalCommandList = dynamic_cast<MetalCommandList*>(&commandList);

        if (metalCommandList == nullptr)
        {
            SetLastError("Metal device can only submit Metal command lists.");
            return false;
        }

        return metalCommandList->CommitAndWait();
    }

    void WaitIdle() override
    {
    }

private:
    void SetLastError(std::string error)
    {
        lastError_ = std::move(error);
    }

private:
    bool enableDebug_ = false;
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> commandQueue_ = nil;
    std::string lastError_;
};
}

std::unique_ptr<RhiDevice> CreateMetalDevice(bool enableDebug)
{
    auto device = std::make_unique<MetalDevice>(enableDebug);

    if (!device->Initialize())
    {
        return nullptr;
    }

    return device;
}
}
