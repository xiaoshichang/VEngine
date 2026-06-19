#include "Engine/RHI/Metal/MetalRhi.h"

#include "Engine/Runtime/Core/Assert.h"

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

        MTLTextureUsage ToMetalTextureUsage(RhiTextureUsage usage)
        {
            MTLTextureUsage metalUsage = MTLTextureUsageUnknown;
            const auto usageValue = static_cast<uint32_t>(usage);

            if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::Sampled)) != 0)
            {
                metalUsage |= MTLTextureUsageShaderRead;
            }

            if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::RenderTarget)) != 0)
            {
                metalUsage |= MTLTextureUsageRenderTarget;
            }

            return metalUsage;
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

        MTLLoadAction ToMetalLoadAction(RhiLoadAction loadAction)
        {
            switch (loadAction)
            {
            case RhiLoadAction::Load:
                return MTLLoadActionLoad;
            case RhiLoadAction::Clear:
                return MTLLoadActionClear;
            case RhiLoadAction::DontCare:
            default:
                return MTLLoadActionDontCare;
            }
        }

        MTLStoreAction ToMetalStoreAction(RhiStoreAction storeAction)
        {
            switch (storeAction)
            {
            case RhiStoreAction::Store:
                return MTLStoreActionStore;
            case RhiStoreAction::DontCare:
            default:
                return MTLStoreActionDontCare;
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

        class MetalTexture final : public RhiTexture
        {
        public:
            MetalTexture(id<MTLTexture> texture, RhiTextureDesc desc)
                : texture_(texture)
                , desc_(desc)
            {
            }

            [[nodiscard]] RhiTextureDimension GetDimension() const noexcept override
            {
                return desc_.dimension;
            }

            [[nodiscard]] uint32_t GetWidth() const noexcept override
            {
                return desc_.width;
            }

            [[nodiscard]] uint32_t GetHeight() const noexcept override
            {
                return desc_.height;
            }

            [[nodiscard]] RhiFormat GetFormat() const noexcept override
            {
                return desc_.format;
            }

            [[nodiscard]] id<MTLTexture> GetNativeTexture() const noexcept
            {
                return texture_;
            }

        private:
            id<MTLTexture> texture_ = nil;
            RhiTextureDesc desc_ = {};
        };

        class MetalFence final : public RhiFence
        {
        public:
            explicit MetalFence(uint64_t initialValue)
                : completedValue_(initialValue)
            {
            }

            void Signal(uint64_t value) noexcept
            {
                if (value > completedValue_)
                {
                    completedValue_ = value;
                }
            }

            [[nodiscard]] bool IsComplete(uint64_t value) const noexcept override
            {
                return completedValue_ >= value;
            }

            [[nodiscard]] bool Wait(uint64_t value) override
            {
                return IsComplete(value);
            }

            [[nodiscard]] uint64_t GetCompletedValue() const noexcept override
            {
                return completedValue_;
            }

        private:
            uint64_t completedValue_ = 0;
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
            MetalPipelineState(RhiPrimitiveTopology topology, RhiFillMode fillMode, id<MTLRenderPipelineState> pipelineState)
                : topology_(topology)
                , fillMode_(fillMode)
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

            [[nodiscard]] RhiFillMode GetFillMode() const noexcept
            {
                return fillMode_;
            }

        private:
            RhiPrimitiveTopology topology_ = RhiPrimitiveTopology::TriangleList;
            RhiFillMode fillMode_ = RhiFillMode::Solid;
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

                if (metalSwapchain == nullptr || commandBuffer_ == nil || desc.colorAttachmentCount == 0)
                {
                    return false;
                }

                const RhiRenderPassColorAttachmentDesc& colorAttachment = desc.colorAttachments[0];
                drawable_ = [metalSwapchain->AcquireDrawable() retain];

                if (drawable_ == nil)
                {
                    return false;
                }

                MTLRenderPassDescriptor* renderPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
                renderPassDescriptor.colorAttachments[0].texture = drawable_.texture;
                renderPassDescriptor.colorAttachments[0].loadAction = ToMetalLoadAction(colorAttachment.loadAction);
                renderPassDescriptor.colorAttachments[0].storeAction = ToMetalStoreAction(colorAttachment.storeAction);
                renderPassDescriptor.colorAttachments[0].clearColor =
                    MTLClearColorMake(colorAttachment.clearColor.r, colorAttachment.clearColor.g, colorAttachment.clearColor.b, colorAttachment.clearColor.a);

                renderCommandEncoder_ = [commandBuffer_ renderCommandEncoderWithDescriptor:renderPassDescriptor];
                return renderCommandEncoder_ != nil;
            }

            void EndRenderPass() override
            {
                [renderCommandEncoder_ endEncoding];
                renderCommandEncoder_ = nil;
            }

            [[nodiscard]] bool CopyTextureToSwapchain(RhiTexture& sourceTexture, RhiSwapchain& swapchain) override
            {
                (void)sourceTexture;
                (void)swapchain;
                return false;
            }

            void SetPipeline(const RhiPipelineState& pipelineState) override
            {
                const auto& metalPipelineState = static_cast<const MetalPipelineState&>(pipelineState);
                [renderCommandEncoder_ setRenderPipelineState:metalPipelineState.GetNativePipelineState()];
                [renderCommandEncoder_ setFrontFacingWinding:MTLWindingClockwise];
                [renderCommandEncoder_ setCullMode:MTLCullModeBack];
                [renderCommandEncoder_ setTriangleFillMode:metalPipelineState.GetFillMode() == RhiFillMode::Wireframe ? MTLTriangleFillModeLines
                                                                                                                        : MTLTriangleFillModeFill];
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

            void SetIndexBuffer(const RhiBuffer& buffer, RhiIndexFormat format, uint64_t offset) override
            {
                const auto& metalBuffer = static_cast<const MetalBuffer&>(buffer);
                indexBuffer_ = metalBuffer.GetNativeBuffer();
                indexBufferOffset_ = offset;
                indexType_ = format == RhiIndexFormat::UInt16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
            }

            void SetUniformBuffer(RhiShaderStage stage, uint32_t slot, const RhiBuffer& buffer, uint64_t offset) override
            {
                const auto& metalBuffer = static_cast<const MetalBuffer&>(buffer);
                switch (stage)
                {
                case RhiShaderStage::Vertex:
                    [renderCommandEncoder_ setVertexBuffer:metalBuffer.GetNativeBuffer() offset:offset atIndex:slot];
                    break;
                case RhiShaderStage::Fragment:
                    [renderCommandEncoder_ setFragmentBuffer:metalBuffer.GetNativeBuffer() offset:offset atIndex:slot];
                    break;
                }
            }

            void Draw(uint32_t vertexCount, uint32_t firstVertex) override
            {
                [renderCommandEncoder_ drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:firstVertex vertexCount:vertexCount];
            }

            void DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) override
            {
                (void)vertexOffset;
                const NSUInteger indexSize = indexType_ == MTLIndexTypeUInt16 ? sizeof(uint16_t) : sizeof(uint32_t);
                [renderCommandEncoder_ drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                                  indexCount:indexCount
                                                   indexType:indexType_
                                                 indexBuffer:indexBuffer_
                                           indexBufferOffset:indexBufferOffset_ + (firstIndex * indexSize)];
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
            id<MTLBuffer> indexBuffer_ = nil;
            uint64_t indexBufferOffset_ = 0;
            MTLIndexType indexType_ = MTLIndexTypeUInt32;
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

                auto swapchain = std::make_unique<MetalSwapchain>(layer, device_, RhiExtent2D{desc.width, desc.height}, desc.colorFormat);

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

            [[nodiscard]] std::unique_ptr<RhiTexture> CreateTexture(const RhiTextureDesc& desc) override
            {
                if (desc.dimension != RhiTextureDimension::Texture2D || desc.width == 0 || desc.height == 0)
                {
                    SetLastError("Metal texture requires a non-empty 2D descriptor.");
                    return nullptr;
                }

                MTLTextureDescriptor* textureDescriptor = [[MTLTextureDescriptor alloc] init];
                textureDescriptor.textureType = MTLTextureType2D;
                textureDescriptor.pixelFormat = ToMetalPixelFormat(desc.format);
                textureDescriptor.width = desc.width;
                textureDescriptor.height = desc.height;
                textureDescriptor.depth = desc.depth;
                textureDescriptor.mipmapLevelCount = desc.mipLevelCount;
                textureDescriptor.usage = ToMetalTextureUsage(desc.usage);
                textureDescriptor.storageMode = MTLStorageModePrivate;

                id<MTLTexture> texture = [device_ newTextureWithDescriptor:textureDescriptor];
                [textureDescriptor release];

                if (texture == nil)
                {
                    SetLastError("MTLDevice newTextureWithDescriptor failed.");
                    return nullptr;
                }

                if (desc.initialData != nullptr && desc.initialDataSize > 0)
                {
                    SetLastError("Metal texture initial data upload is not implemented in the minimum RHI path.");
                    return nullptr;
                }

                return std::make_unique<MetalTexture>(texture, desc);
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

            [[nodiscard]] std::unique_ptr<RhiPipelineState> CreateGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) override
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
                id<MTLRenderPipelineState> pipelineState = [device_ newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];

                [pipelineDescriptor release];
                [vertexDescriptor release];

                if (pipelineState == nil)
                {
                    SetLastError(ToString(error));
                    return nullptr;
                }

                return std::make_unique<MetalPipelineState>(desc.topology, desc.fillMode, pipelineState);
            }

            [[nodiscard]] std::unique_ptr<RhiCommandList> CreateCommandList() override
            {
                return std::make_unique<MetalCommandList>(commandQueue_);
            }

            [[nodiscard]] std::unique_ptr<RhiFence> CreateFence(uint64_t initialValue = 0) override
            {
                return std::make_unique<MetalFence>(initialValue);
            }

            [[nodiscard]] bool SignalFence(RhiFence& fence, uint64_t value) override
            {
                auto* metalFence = dynamic_cast<MetalFence*>(&fence);
                if (metalFence == nullptr)
                {
                    SetLastError("Metal device can only signal Metal fences.");
                    return false;
                }

                metalFence->Signal(value);
                return true;
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

            void WaitIdle() override {}

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
    } // namespace

    std::unique_ptr<RhiDevice> CreateMetalDevice(bool enableDebug)
    {
        auto device = std::make_unique<MetalDevice>(enableDebug);

        if (!device->Initialize())
        {
            return nullptr;
        }

        return device;
    }
} // namespace ve::rhi
