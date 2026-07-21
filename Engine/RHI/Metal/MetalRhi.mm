#include "Engine/RHI/Metal/MetalRhi.h"

#include "Engine/RHI/Common/RhiUtils.h"
#include "Engine/Runtime/Core/Assert.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ve::rhi
{
    namespace
    {
        inline constexpr uint32_t MetalVertexBufferBaseIndex = 16;

        MTLPixelFormat ToMetalPixelFormat(RhiFormat format)
        {
            switch (format)
            {
            case RhiFormat::Rgba8Unorm:
                return MTLPixelFormatRGBA8Unorm;
            case RhiFormat::Bgra8Unorm:
                return MTLPixelFormatBGRA8Unorm;
            case RhiFormat::Depth32Float:
                return MTLPixelFormatDepth32Float;
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

            if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::DepthStencil)) != 0)
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
            case RhiFormat::Depth32Float:
            case RhiFormat::Unknown:
            default:
                return MTLVertexFormatInvalid;
            }
        }

        MTLPrimitiveType ToMetalPrimitiveType(RhiPrimitiveTopology topology)
        {
            switch (topology)
            {
            case RhiPrimitiveTopology::LineList:
                return MTLPrimitiveTypeLine;
            case RhiPrimitiveTopology::TriangleList:
            default:
                return MTLPrimitiveTypeTriangle;
            }
        }

        MTLSamplerMinMagFilter ToMetalMinMagFilter(RhiSamplerFilter filter)
        {
            switch (filter)
            {
            case RhiSamplerFilter::Point:
                return MTLSamplerMinMagFilterNearest;
            case RhiSamplerFilter::Bilinear:
            case RhiSamplerFilter::Trilinear:
            case RhiSamplerFilter::Anisotropic:
            default:
                return MTLSamplerMinMagFilterLinear;
            }
        }

        MTLSamplerMipFilter ToMetalMipFilter(RhiSamplerFilter filter)
        {
            switch (filter)
            {
            case RhiSamplerFilter::Trilinear:
            case RhiSamplerFilter::Anisotropic:
                return MTLSamplerMipFilterLinear;
            case RhiSamplerFilter::Point:
            case RhiSamplerFilter::Bilinear:
            default:
                return MTLSamplerMipFilterNearest;
            }
        }

        MTLSamplerAddressMode ToMetalAddressMode(RhiSamplerAddressMode mode)
        {
            switch (mode)
            {
            case RhiSamplerAddressMode::Mirror:
                return MTLSamplerAddressModeMirrorRepeat;
            case RhiSamplerAddressMode::Clamp:
                return MTLSamplerAddressModeClampToEdge;
            case RhiSamplerAddressMode::Border:
                return MTLSamplerAddressModeClampToEdge;
            case RhiSamplerAddressMode::Wrap:
            default:
                return MTLSamplerAddressModeRepeat;
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

        MTLClearColor ToMetalClearColor(const RhiColor& color) noexcept
        {
            return MTLClearColorMake(color.r, color.g, color.b, color.a);
        }

        MTLCullMode ToMetalCullMode(RhiCullMode cullMode)
        {
            switch (cullMode)
            {
            case RhiCullMode::None:
                return MTLCullModeNone;
            case RhiCullMode::Front:
                return MTLCullModeFront;
            case RhiCullMode::Back:
            default:
                return MTLCullModeBack;
            }
        }

        MTLCompareFunction ToMetalCompareFunction(RhiCompareFunction compareFunction)
        {
            switch (compareFunction)
            {
            case RhiCompareFunction::Never:
                return MTLCompareFunctionNever;
            case RhiCompareFunction::Less:
                return MTLCompareFunctionLess;
            case RhiCompareFunction::Equal:
                return MTLCompareFunctionEqual;
            case RhiCompareFunction::Always:
                return MTLCompareFunctionAlways;
            case RhiCompareFunction::LessEqual:
                return MTLCompareFunctionLessEqual;
            case RhiCompareFunction::Greater:
                return MTLCompareFunctionGreater;
            case RhiCompareFunction::NotEqual:
                return MTLCompareFunctionNotEqual;
            case RhiCompareFunction::GreaterEqual:
            default:
                return MTLCompareFunctionGreaterEqual;
            }
        }

        MTLStencilOperation ToMetalStencilOperation(RhiStencilOperation operation)
        {
            switch (operation)
            {
            case RhiStencilOperation::Keep:
                return MTLStencilOperationKeep;
            case RhiStencilOperation::Zero:
                return MTLStencilOperationZero;
            case RhiStencilOperation::Replace:
                return MTLStencilOperationReplace;
            case RhiStencilOperation::IncrementClamp:
                return MTLStencilOperationIncrementClamp;
            case RhiStencilOperation::DecrementClamp:
                return MTLStencilOperationDecrementClamp;
            case RhiStencilOperation::Invert:
                return MTLStencilOperationInvert;
            case RhiStencilOperation::IncrementWrap:
                return MTLStencilOperationIncrementWrap;
            case RhiStencilOperation::DecrementWrap:
            default:
                return MTLStencilOperationDecrementWrap;
            }
        }

        MTLBlendFactor ToMetalBlendFactor(RhiBlendFactor factor)
        {
            switch (factor)
            {
            case RhiBlendFactor::Zero:
                return MTLBlendFactorZero;
            case RhiBlendFactor::One:
                return MTLBlendFactorOne;
            case RhiBlendFactor::SourceColor:
                return MTLBlendFactorSourceColor;
            case RhiBlendFactor::OneMinusSourceColor:
                return MTLBlendFactorOneMinusSourceColor;
            case RhiBlendFactor::SourceAlpha:
                return MTLBlendFactorSourceAlpha;
            case RhiBlendFactor::OneMinusSourceAlpha:
                return MTLBlendFactorOneMinusSourceAlpha;
            case RhiBlendFactor::DestinationColor:
                return MTLBlendFactorDestinationColor;
            case RhiBlendFactor::OneMinusDestinationColor:
                return MTLBlendFactorOneMinusDestinationColor;
            case RhiBlendFactor::DestinationAlpha:
                return MTLBlendFactorDestinationAlpha;
            case RhiBlendFactor::OneMinusDestinationAlpha:
            default:
                return MTLBlendFactorOneMinusDestinationAlpha;
            }
        }

        MTLBlendOperation ToMetalBlendOperation(RhiBlendOperation operation)
        {
            switch (operation)
            {
            case RhiBlendOperation::Add:
                return MTLBlendOperationAdd;
            case RhiBlendOperation::Subtract:
                return MTLBlendOperationSubtract;
            case RhiBlendOperation::ReverseSubtract:
                return MTLBlendOperationReverseSubtract;
            case RhiBlendOperation::Min:
                return MTLBlendOperationMin;
            case RhiBlendOperation::Max:
            default:
                return MTLBlendOperationMax;
            }
        }

        MTLColorWriteMask ToMetalColorWriteMask(uint8_t mask)
        {
            MTLColorWriteMask metalMask = 0;
            if ((mask & RhiColorWriteRed) != 0)
            {
                metalMask |= MTLColorWriteMaskRed;
            }
            if ((mask & RhiColorWriteGreen) != 0)
            {
                metalMask |= MTLColorWriteMaskGreen;
            }
            if ((mask & RhiColorWriteBlue) != 0)
            {
                metalMask |= MTLColorWriteMaskBlue;
            }
            if ((mask & RhiColorWriteAlpha) != 0)
            {
                metalMask |= MTLColorWriteMaskAlpha;
            }
            return metalMask;
        }

        void ApplyMetalStencilFaceDesc(MTLStencilDescriptor* metalDesc, const RhiStencilFaceDesc& desc)
        {
            metalDesc.stencilFailureOperation = ToMetalStencilOperation(desc.failOperation);
            metalDesc.depthFailureOperation = ToMetalStencilOperation(desc.depthFailOperation);
            metalDesc.depthStencilPassOperation = ToMetalStencilOperation(desc.passOperation);
            metalDesc.stencilCompareFunction = ToMetalCompareFunction(desc.compareFunction);
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
            MetalBuffer(id<MTLBuffer> buffer, uint64_t size, RhiBufferMemoryUsage memoryUsage)
                : buffer_(buffer)
                , size_(size)
                , memoryUsage_(memoryUsage)
            {
            }

            ~MetalBuffer() override
            {
                [buffer_ release];
            }

            [[nodiscard]] uint64_t GetSize() const noexcept override
            {
                return size_;
            }

            [[nodiscard]] id<MTLBuffer> GetNativeBuffer() const noexcept
            {
                return buffer_;
            }

            [[nodiscard]] RhiBufferMemoryUsage GetMemoryUsage() const noexcept
            {
                return memoryUsage_;
            }

        private:
            id<MTLBuffer> buffer_ = nil;
            uint64_t size_ = 0;
            RhiBufferMemoryUsage memoryUsage_ = RhiBufferMemoryUsage::GpuOnly;
        };

        class MetalTexture final : public RhiTexture
        {
        public:
            MetalTexture(id<MTLTexture> texture, RhiTextureDesc desc)
                : texture_(texture)
                , desc_(desc)
            {
            }

            ~MetalTexture() override
            {
                [texture_ release];
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

            [[nodiscard]] void* GetNativeSampledViewHandle() const noexcept override
            {
                return texture_;
            }

        private:
            id<MTLTexture> texture_ = nil;
            RhiTextureDesc desc_ = {};
        };

        class MetalSampler final : public RhiSampler
        {
        public:
            MetalSampler(id<MTLSamplerState> samplerState, RhiSamplerDesc desc)
                : samplerState_(samplerState)
                , desc_(desc)
            {
            }

            ~MetalSampler() override
            {
                [samplerState_ release];
            }

            [[nodiscard]] RhiSamplerFilter GetFilter() const noexcept override
            {
                return desc_.filter;
            }

            [[nodiscard]] id<MTLSamplerState> GetNativeSamplerState() const noexcept
            {
                return samplerState_;
            }

        private:
            id<MTLSamplerState> samplerState_ = nil;
            RhiSamplerDesc desc_ = {};
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
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (value > completedValue_)
                    {
                        completedValue_ = value;
                    }
                }
                condition_.notify_all();
            }

            [[nodiscard]] bool IsComplete(uint64_t value) const noexcept override
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return completedValue_ >= value;
            }

            [[nodiscard]] bool Wait(uint64_t value) override
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this, value]() { return completedValue_ >= value; });
                return true;
            }

            [[nodiscard]] uint64_t GetCompletedValue() const noexcept override
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return completedValue_;
            }

        private:
            mutable std::mutex mutex_;
            std::condition_variable condition_;
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

            ~MetalShaderModule() override
            {
                [function_ release];
                [library_ release];
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
            MetalPipelineState(RhiPrimitiveTopology topology,
                               RhiRasterizerStateDesc rasterizerState,
                               id<MTLRenderPipelineState> pipelineState,
                               id<MTLDepthStencilState> depthStencilState,
                               std::vector<RhiPipelineResourceBindingDesc> resourceBindings)
                : topology_(topology)
                , rasterizerState_(rasterizerState)
                , pipelineState_(pipelineState)
                , depthStencilState_(depthStencilState)
                , resourceBindings_(std::move(resourceBindings))
            {
            }

            ~MetalPipelineState() override
            {
                [depthStencilState_ release];
                [pipelineState_ release];
            }

            [[nodiscard]] RhiPrimitiveTopology GetTopology() const noexcept override
            {
                return topology_;
            }

            [[nodiscard]] id<MTLRenderPipelineState> GetNativePipelineState() const noexcept
            {
                return pipelineState_;
            }

            [[nodiscard]] const RhiRasterizerStateDesc& GetRasterizerState() const noexcept
            {
                return rasterizerState_;
            }

            [[nodiscard]] id<MTLDepthStencilState> GetNativeDepthStencilState() const noexcept
            {
                return depthStencilState_;
            }

            [[nodiscard]] bool HasBinding(RhiPipelineResourceKind kind, RhiShaderStage stage, uint32_t slot) const noexcept
            {
                for (const RhiPipelineResourceBindingDesc& binding : resourceBindings_)
                {
                    if (binding.kind == kind && binding.stage == stage && binding.slot == slot)
                    {
                        return true;
                    }
                }
                return false;
            }

        private:
            RhiPrimitiveTopology topology_ = RhiPrimitiveTopology::TriangleList;
            RhiRasterizerStateDesc rasterizerState_ = {};
            id<MTLRenderPipelineState> pipelineState_ = nil;
            id<MTLDepthStencilState> depthStencilState_ = nil;
            std::vector<RhiPipelineResourceBindingDesc> resourceBindings_;
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
                layer_.framebufferOnly = NO;
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

            [[nodiscard]] uint32_t GetBufferCount() const noexcept override
            {
                return 3;
            }

            [[nodiscard]] bool Resize(RhiExtent2D extent) override
            {
                if (extent.width == 0 || extent.height == 0)
                {
                    return false;
                }

                extent_ = extent;
                layer_.drawableSize = CGSizeMake(extent.width, extent.height);
                return true;
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

            ~MetalCommandList() override
            {
                ResetTransientState();
            }

            [[nodiscard]] bool Begin() override
            {
                ResetTransientState();
                activePipeline_ = nullptr;
                @autoreleasepool
                {
                    commandBuffer_ = [[commandQueue_ commandBuffer] retain];
                }
                return commandBuffer_ != nil;
            }

            [[nodiscard]] bool End() override
            {
                return commandBuffer_ != nil;
            }

            [[nodiscard]] bool BeginRenderPass(RhiSwapchain& swapchain, const RhiRenderPassBeginInfo& beginInfo) override
            {
                @autoreleasepool
                {
                    auto* metalSwapchain = dynamic_cast<MetalSwapchain*>(&swapchain);

                    if (metalSwapchain == nullptr || commandBuffer_ == nil || (!beginInfo.hasColorAttachment && beginInfo.colorAttachmentIsSwapchain) ||
                        (!beginInfo.hasColorAttachment && beginInfo.colorAttachment.texture != nullptr) ||
                        (!beginInfo.hasColorAttachment && !beginInfo.hasDepthAttachment))
                    {
                        return false;
                    }

                    const RhiRenderPassColorAttachmentInfo& colorAttachment = beginInfo.colorAttachment;
                    const bool targetsSwapchain = beginInfo.hasColorAttachment && beginInfo.colorAttachmentIsSwapchain;
                    id<MTLTexture> colorTexture = nil;
                    if (beginInfo.hasColorAttachment && beginInfo.colorAttachmentIsSwapchain)
                    {
                        if (colorAttachment.texture != nullptr || drawable_ != nil)
                        {
                            return false;
                        }

                        drawable_ = [metalSwapchain->AcquireDrawable() retain];
                        if (drawable_ == nil)
                        {
                            return false;
                        }
                        colorTexture = drawable_.texture;
                    }
                    else if (beginInfo.hasColorAttachment)
                    {
                        auto* metalTexture = dynamic_cast<MetalTexture*>(colorAttachment.texture);
                        if (metalTexture == nullptr)
                        {
                            return false;
                        }

                        colorTexture = metalTexture->GetNativeTexture();
                    }

                    if (beginInfo.hasColorAttachment && colorTexture == nil)
                    {
                        if (targetsSwapchain)
                        {
                            [drawable_ release];
                            drawable_ = nil;
                        }
                        return false;
                    }

                    id<MTLTexture> depthTexture = nil;
                    if (beginInfo.hasDepthAttachment)
                    {
                        auto* metalDepthTexture = dynamic_cast<MetalTexture*>(beginInfo.depthAttachment.texture);
                        if (metalDepthTexture == nullptr)
                        {
                            if (targetsSwapchain)
                            {
                                [drawable_ release];
                                drawable_ = nil;
                            }
                            return false;
                        }

                        depthTexture = metalDepthTexture->GetNativeTexture();
                        if (depthTexture == nil)
                        {
                            if (targetsSwapchain)
                            {
                                [drawable_ release];
                                drawable_ = nil;
                            }
                            return false;
                        }
                    }

                    MTLRenderPassDescriptor* renderPassDescriptor = [[MTLRenderPassDescriptor alloc] init];
                    if (beginInfo.hasColorAttachment)
                    {
                        renderPassDescriptor.colorAttachments[0].texture = colorTexture;
                        renderPassDescriptor.colorAttachments[0].loadAction = ToMetalLoadAction(colorAttachment.loadAction);
                        renderPassDescriptor.colorAttachments[0].storeAction = ToMetalStoreAction(colorAttachment.storeAction);
                        renderPassDescriptor.colorAttachments[0].clearColor = ToMetalClearColor(colorAttachment.clearColor);
                    }

                    if (depthTexture != nil)
                    {
                        renderPassDescriptor.depthAttachment.texture = depthTexture;
                        renderPassDescriptor.depthAttachment.loadAction = ToMetalLoadAction(beginInfo.depthAttachment.loadAction);
                        renderPassDescriptor.depthAttachment.storeAction = ToMetalStoreAction(beginInfo.depthAttachment.storeAction);
                        renderPassDescriptor.depthAttachment.clearDepth = beginInfo.depthAttachment.clearDepth;
                    }

                    renderCommandEncoder_ = [[commandBuffer_ renderCommandEncoderWithDescriptor:renderPassDescriptor] retain];
                    [renderPassDescriptor release];

                    if (renderCommandEncoder_ == nil)
                    {
                        if (targetsSwapchain)
                        {
                            [drawable_ release];
                            drawable_ = nil;
                        }
                        return false;
                    }

                    if (beginInfo.debugName != nullptr)
                    {
                        renderCommandEncoder_.label = [NSString stringWithUTF8String:beginInfo.debugName];
                    }

                    return true;
                }
            }

            void EndRenderPass() override
            {
                [renderCommandEncoder_ endEncoding];
                [renderCommandEncoder_ release];
                renderCommandEncoder_ = nil;
            }

            [[nodiscard]] bool CopyTextureToSwapchain(RhiTexture& sourceTexture, RhiSwapchain& swapchain) override
            {
                auto* metalTexture = dynamic_cast<MetalTexture*>(&sourceTexture);
                auto* metalSwapchain = dynamic_cast<MetalSwapchain*>(&swapchain);
                if (metalTexture == nullptr || metalSwapchain == nullptr || commandBuffer_ == nil)
                {
                    return false;
                }

                const RhiExtent2D swapchainExtent = metalSwapchain->GetExtent();
                if (sourceTexture.GetWidth() != swapchainExtent.width || sourceTexture.GetHeight() != swapchainExtent.height ||
                    sourceTexture.GetFormat() != metalSwapchain->GetColorFormat())
                {
                    return false;
                }

                if (drawable_ != nil)
                {
                    return false;
                }

                drawable_ = [metalSwapchain->AcquireDrawable() retain];
                if (drawable_ == nil)
                {
                    return false;
                }

                id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer_ blitCommandEncoder];
                if (blitEncoder == nil)
                {
                    [drawable_ release];
                    drawable_ = nil;
                    return false;
                }

                const MTLSize copySize = MTLSizeMake(sourceTexture.GetWidth(), sourceTexture.GetHeight(), 1);
                [blitEncoder copyFromTexture:metalTexture->GetNativeTexture()
                                 sourceSlice:0
                                 sourceLevel:0
                                sourceOrigin:MTLOriginMake(0, 0, 0)
                                  sourceSize:copySize
                                   toTexture:drawable_.texture
                            destinationSlice:0
                            destinationLevel:0
                           destinationOrigin:MTLOriginMake(0, 0, 0)];
                [blitEncoder endEncoding];
                return true;
            }

            void SetPipeline(const RhiPipelineState& pipelineState) override
            {
                const auto& metalPipelineState = static_cast<const MetalPipelineState&>(pipelineState);
                activePipeline_ = &metalPipelineState;
                [renderCommandEncoder_ setRenderPipelineState:metalPipelineState.GetNativePipelineState()];
                [renderCommandEncoder_ setDepthStencilState:metalPipelineState.GetNativeDepthStencilState()];

                const RhiRasterizerStateDesc& rasterizerState = metalPipelineState.GetRasterizerState();
                [renderCommandEncoder_ setFrontFacingWinding:rasterizerState.frontCounterClockwise ? MTLWindingCounterClockwise : MTLWindingClockwise];
                [renderCommandEncoder_ setCullMode:ToMetalCullMode(rasterizerState.cullMode)];
                [renderCommandEncoder_
                    setTriangleFillMode:rasterizerState.fillMode == RhiFillMode::Wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
                constexpr Float32 D3DDepthBiasUnit = 1.0f / 8388608.0f;
                [renderCommandEncoder_ setDepthBias:static_cast<Float32>(rasterizerState.depthBias) * D3DDepthBiasUnit
                                         slopeScale:rasterizerState.slopeScaledDepthBias
                                              clamp:rasterizerState.depthBiasClamp];
                primitiveType_ = ToMetalPrimitiveType(metalPipelineState.GetTopology());
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
                [renderCommandEncoder_ setVertexBuffer:metalBuffer.GetNativeBuffer() offset:offset atIndex:MetalVertexBufferBaseIndex + slot];
            }

            void SetIndexBuffer(const RhiBuffer& buffer, RhiIndexFormat format, uint64_t offset) override
            {
                const auto& metalBuffer = static_cast<const MetalBuffer&>(buffer);
                indexBuffer_ = metalBuffer.GetNativeBuffer();
                indexBufferOffset_ = offset;
                indexType_ = format == RhiIndexFormat::UInt16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
            }

            void SetUniformBuffer(RhiShaderStage stage, uint32_t slot, const RhiBuffer& buffer, uint64_t offset, uint64_t size) override
            {
                if (!ValidateBinding(RhiPipelineResourceKind::UniformBuffer, stage, slot))
                {
                    return;
                }

                VE_ASSERT(size > 0);
                VE_ASSERT(offset + size <= buffer.GetSize());
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

            void SetTexture(RhiShaderStage stage, uint32_t slot, const RhiTexture& texture) override
            {
                if (!ValidateBinding(RhiPipelineResourceKind::SampledTexture, stage, slot))
                {
                    return;
                }

                const auto& metalTexture = static_cast<const MetalTexture&>(texture);
                switch (stage)
                {
                case RhiShaderStage::Vertex:
                    [renderCommandEncoder_ setVertexTexture:metalTexture.GetNativeTexture() atIndex:slot];
                    break;
                case RhiShaderStage::Fragment:
                    [renderCommandEncoder_ setFragmentTexture:metalTexture.GetNativeTexture() atIndex:slot];
                    break;
                }
            }

            void SetSampler(RhiShaderStage stage, uint32_t slot, const RhiSampler& sampler) override
            {
                if (!ValidateBinding(RhiPipelineResourceKind::Sampler, stage, slot))
                {
                    return;
                }

                const auto& metalSampler = static_cast<const MetalSampler&>(sampler);
                switch (stage)
                {
                case RhiShaderStage::Vertex:
                    [renderCommandEncoder_ setVertexSamplerState:metalSampler.GetNativeSamplerState() atIndex:slot];
                    break;
                case RhiShaderStage::Fragment:
                    [renderCommandEncoder_ setFragmentSamplerState:metalSampler.GetNativeSamplerState() atIndex:slot];
                    break;
                }
            }

            void Draw(uint32_t vertexCount, uint32_t firstVertex) override
            {
                [renderCommandEncoder_ drawPrimitives:primitiveType_ vertexStart:firstVertex vertexCount:vertexCount];
            }

            void DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) override
            {
                (void)vertexOffset;
                const NSUInteger indexSize = indexType_ == MTLIndexTypeUInt16 ? sizeof(uint16_t) : sizeof(uint32_t);
                [renderCommandEncoder_ drawIndexedPrimitives:primitiveType_
                                                  indexCount:indexCount
                                                   indexType:indexType_
                                                 indexBuffer:indexBuffer_
                                           indexBufferOffset:indexBufferOffset_ + (firstIndex * indexSize)];
            }

            [[nodiscard]] void* GetNativeRenderEncoderHandle() const noexcept override
            {
                return renderCommandEncoder_;
            }

            [[nodiscard]] void* GetNativeCommandBufferHandle() const noexcept override
            {
                return commandBuffer_;
            }

            [[nodiscard]] bool Commit(MetalFence* completionFence, uint64_t completionValue)
            {
                if (commandBuffer_ == nil)
                {
                    return false;
                }

                if (drawable_ != nil)
                {
                    [commandBuffer_ presentDrawable:drawable_];
                }

                if (completionFence != nullptr)
                {
                    [commandBuffer_ addCompletedHandler:^(id<MTLCommandBuffer>) {
                      completionFence->Signal(completionValue);
                    }];
                }

                [commandBuffer_ commit];

                [drawable_ release];
                drawable_ = nil;
                [commandBuffer_ release];
                commandBuffer_ = nil;
                return true;
            }

        private:
            [[nodiscard]] bool ValidateBinding(RhiPipelineResourceKind kind, RhiShaderStage stage, uint32_t slot) const noexcept
            {
                const bool valid = activePipeline_ != nullptr && activePipeline_->HasBinding(kind, stage, slot);
                VE_ASSERT_MESSAGE(valid, "Metal resource binding is absent from the active pipeline layout.");
                return valid;
            }

            void ResetTransientState() noexcept
            {
                if (renderCommandEncoder_ != nil)
                {
                    [renderCommandEncoder_ endEncoding];
                    [renderCommandEncoder_ release];
                    renderCommandEncoder_ = nil;
                }

                [drawable_ release];
                drawable_ = nil;
                [commandBuffer_ release];
                commandBuffer_ = nil;
            }

            id<MTLCommandQueue> commandQueue_ = nil;
            id<MTLCommandBuffer> commandBuffer_ = nil;
            id<MTLRenderCommandEncoder> renderCommandEncoder_ = nil;
            id<CAMetalDrawable> drawable_ = nil;
            id<MTLBuffer> indexBuffer_ = nil;
            uint64_t indexBufferOffset_ = 0;
            MTLIndexType indexType_ = MTLIndexTypeUInt32;
            MTLPrimitiveType primitiveType_ = MTLPrimitiveTypeTriangle;
            const MetalPipelineState* activePipeline_ = nullptr;
        };

        class MetalDevice final : public RhiDevice
        {
        public:
            explicit MetalDevice(bool enableDebug)
                : enableDebug_(enableDebug)
            {
            }

            ~MetalDevice() override
            {
                [commandQueue_ release];
                [device_ release];
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

                return std::make_unique<MetalBuffer>(buffer, desc.size, desc.memoryUsage);
            }

            void UpdateBuffer(RhiBuffer& buffer, uint64_t offset, const void* data, uint64_t size, RhiBufferUpdateMode updateMode) override
            {
                static_cast<void>(updateMode);
                auto& metalBuffer = static_cast<MetalBuffer&>(buffer);
                VE_ASSERT(metalBuffer.GetMemoryUsage() == RhiBufferMemoryUsage::CpuToGpu);
                VE_ASSERT(data != nullptr);
                VE_ASSERT(size > 0);
                VE_ASSERT(offset + size <= metalBuffer.GetSize());
                std::memcpy(static_cast<std::byte*>([metalBuffer.GetNativeBuffer() contents]) + offset, data, static_cast<size_t>(size));
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
                textureDescriptor.storageMode = desc.initialData != nullptr ? MTLStorageModeShared : MTLStorageModePrivate;

                id<MTLTexture> texture = [device_ newTextureWithDescriptor:textureDescriptor];
                [textureDescriptor release];

                if (texture == nil)
                {
                    SetLastError("MTLDevice newTextureWithDescriptor failed.");
                    return nullptr;
                }

                if (desc.initialData != nullptr && desc.initialDataSize > 0)
                {
                    if (desc.mipLevelCount != 1 || desc.initialDataRowPitch == 0)
                    {
                        SetLastError("Metal texture initial upload requires one mip level and a row pitch.");
                        [texture release];
                        return nullptr;
                    }

                    MTLRegion region = MTLRegionMake2D(0, 0, desc.width, desc.height);
                    [texture replaceRegion:region mipmapLevel:0 withBytes:desc.initialData bytesPerRow:desc.initialDataRowPitch];
                }

                return std::make_unique<MetalTexture>(texture, desc);
            }

            [[nodiscard]] std::unique_ptr<RhiSampler> CreateSampler(const RhiSamplerDesc& desc) override
            {
                MTLSamplerDescriptor* samplerDescriptor = [[MTLSamplerDescriptor alloc] init];
                samplerDescriptor.minFilter = ToMetalMinMagFilter(desc.filter);
                samplerDescriptor.magFilter = ToMetalMinMagFilter(desc.filter);
                samplerDescriptor.mipFilter = ToMetalMipFilter(desc.filter);
                samplerDescriptor.sAddressMode = ToMetalAddressMode(desc.addressU);
                samplerDescriptor.tAddressMode = ToMetalAddressMode(desc.addressV);
                samplerDescriptor.rAddressMode = ToMetalAddressMode(desc.addressW);
                samplerDescriptor.lodMinClamp = desc.minLod;
                samplerDescriptor.lodMaxClamp = desc.maxLod;
                samplerDescriptor.maxAnisotropy = desc.maxAnisotropy;
                if (desc.reductionMode == RhiSamplerReductionMode::Comparison)
                {
                    samplerDescriptor.compareFunction = ToMetalCompareFunction(desc.comparisonFunction);
                }

                id<MTLSamplerState> samplerState = [device_ newSamplerStateWithDescriptor:samplerDescriptor];
                [samplerDescriptor release];

                if (samplerState == nil)
                {
                    SetLastError("MTLDevice newSamplerStateWithDescriptor failed.");
                    return nullptr;
                }

                return std::make_unique<MetalSampler>(samplerState, desc);
            }

            [[nodiscard]] std::unique_ptr<RhiShaderModule> CreateShaderModule(const RhiShaderModuleDesc& desc) override
            {
                if (desc.source == nullptr || desc.entryPoint == nullptr)
                {
                    SetLastError("Metal shader module requires source and entry point.");
                    return nullptr;
                }

                NSString* source = [[NSString alloc] initWithUTF8String:desc.source];
                NSError* error = nil;
                id<MTLLibrary> library = [device_ newLibraryWithSource:source options:nil error:&error];
                [source release];

                if (library == nil)
                {
                    SetLastError(ToString(error));
                    return nullptr;
                }

                NSString* functionName = [[NSString alloc] initWithUTF8String:desc.entryPoint];
                id<MTLFunction> function = [library newFunctionWithName:functionName];
                [functionName release];

                if (function == nil)
                {
                    SetLastError("MTLLibrary newFunctionWithName failed.");
                    [library release];
                    return nullptr;
                }

                return std::make_unique<MetalShaderModule>(desc.stage, library, function);
            }

            [[nodiscard]] std::unique_ptr<RhiPipelineState> CreateGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) override
            {
                if (!IsPipelineResourceLayoutValid(desc.resourceLayout))
                {
                    SetLastError("Metal graphics pipeline resource layout is invalid or contains duplicate bindings.");
                    return nullptr;
                }
                if (desc.colorAttachmentCount > 1 || (desc.colorAttachmentCount == 0 && desc.colorFormat != RhiFormat::Unknown) ||
                    (desc.colorAttachmentCount == 1 && (desc.colorFormat == RhiFormat::Unknown || desc.colorFormat == RhiFormat::Depth32Float)))
                {
                    SetLastError("Metal graphics pipeline requires zero or one valid color attachment format.");
                    return nullptr;
                }

                const RhiBoundShaderStateDesc& boundShaderState = desc.boundShaderState;
                const auto* vertexShaderModule = dynamic_cast<const MetalShaderModule*>(boundShaderState.vertexShader);
                const auto* fragmentShaderModule =
                    boundShaderState.fragmentShader != nullptr ? dynamic_cast<const MetalShaderModule*>(boundShaderState.fragmentShader) : nullptr;

                if (vertexShaderModule == nullptr || vertexShaderModule->GetStage() != RhiShaderStage::Vertex ||
                    (boundShaderState.fragmentShader != nullptr &&
                     (fragmentShaderModule == nullptr || fragmentShaderModule->GetStage() != RhiShaderStage::Fragment)) ||
                    (desc.colorAttachmentCount == 1 && fragmentShaderModule == nullptr))
                {
                    SetLastError("Metal graphics pipeline requires a Metal vertex shader and a fragment shader for color output.");
                    return nullptr;
                }

                MTLVertexDescriptor* vertexDescriptor = [[MTLVertexDescriptor alloc] init];

                for (uint32_t index = 0; index < boundShaderState.vertexDeclaration.attributeCount; ++index)
                {
                    const RhiVertexAttributeDesc& attribute = boundShaderState.vertexDeclaration.attributes[index];
                    vertexDescriptor.attributes[index].format = ToMetalVertexFormat(attribute.format);
                    vertexDescriptor.attributes[index].offset = attribute.offset;
                    vertexDescriptor.attributes[index].bufferIndex = MetalVertexBufferBaseIndex;
                }

                vertexDescriptor.layouts[MetalVertexBufferBaseIndex].stride = boundShaderState.vertexDeclaration.stride;
                vertexDescriptor.layouts[MetalVertexBufferBaseIndex].stepFunction = MTLVertexStepFunctionPerVertex;
                vertexDescriptor.layouts[MetalVertexBufferBaseIndex].stepRate = 1;

                MTLRenderPipelineDescriptor* pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
                pipelineDescriptor.vertexFunction = vertexShaderModule->GetFunction();
                pipelineDescriptor.fragmentFunction = fragmentShaderModule != nullptr ? fragmentShaderModule->GetFunction() : nil;
                pipelineDescriptor.vertexDescriptor = vertexDescriptor;
                pipelineDescriptor.colorAttachments[0].pixelFormat =
                    desc.colorAttachmentCount == 0 ? MTLPixelFormatInvalid : ToMetalPixelFormat(desc.colorFormat);
                if (desc.colorAttachmentCount == 1)
                {
                    const RhiBlendRenderTargetDesc& colorBlendDesc = desc.blendState.renderTargets[0];
                    pipelineDescriptor.colorAttachments[0].blendingEnabled = colorBlendDesc.blendEnabled ? YES : NO;
                    pipelineDescriptor.colorAttachments[0].sourceRGBBlendFactor = ToMetalBlendFactor(colorBlendDesc.sourceColorBlendFactor);
                    pipelineDescriptor.colorAttachments[0].destinationRGBBlendFactor = ToMetalBlendFactor(colorBlendDesc.destinationColorBlendFactor);
                    pipelineDescriptor.colorAttachments[0].rgbBlendOperation = ToMetalBlendOperation(colorBlendDesc.colorBlendOperation);
                    pipelineDescriptor.colorAttachments[0].sourceAlphaBlendFactor = ToMetalBlendFactor(colorBlendDesc.sourceAlphaBlendFactor);
                    pipelineDescriptor.colorAttachments[0].destinationAlphaBlendFactor = ToMetalBlendFactor(colorBlendDesc.destinationAlphaBlendFactor);
                    pipelineDescriptor.colorAttachments[0].alphaBlendOperation = ToMetalBlendOperation(colorBlendDesc.alphaBlendOperation);
                    pipelineDescriptor.colorAttachments[0].writeMask = ToMetalColorWriteMask(colorBlendDesc.colorWriteMask);
                }
                pipelineDescriptor.depthAttachmentPixelFormat =
                    desc.depthStencilState.depthTestEnabled ? ToMetalPixelFormat(desc.depthFormat) : MTLPixelFormatInvalid;

                NSError* error = nil;
                id<MTLRenderPipelineState> pipelineState = [device_ newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];

                [pipelineDescriptor release];
                [vertexDescriptor release];

                if (pipelineState == nil)
                {
                    SetLastError(ToString(error));
                    return nullptr;
                }

                MTLDepthStencilDescriptor* depthStencilDescriptor = [[MTLDepthStencilDescriptor alloc] init];
                depthStencilDescriptor.depthCompareFunction =
                    desc.depthStencilState.depthTestEnabled ? ToMetalCompareFunction(desc.depthStencilState.depthCompareFunction) : MTLCompareFunctionAlways;
                depthStencilDescriptor.depthWriteEnabled = desc.depthStencilState.depthWriteEnabled ? YES : NO;
                if (desc.depthStencilState.stencilEnabled)
                {
                    depthStencilDescriptor.frontFaceStencil.readMask = desc.depthStencilState.stencilReadMask;
                    depthStencilDescriptor.frontFaceStencil.writeMask = desc.depthStencilState.stencilWriteMask;
                    depthStencilDescriptor.backFaceStencil.readMask = desc.depthStencilState.stencilReadMask;
                    depthStencilDescriptor.backFaceStencil.writeMask = desc.depthStencilState.stencilWriteMask;
                    ApplyMetalStencilFaceDesc(depthStencilDescriptor.frontFaceStencil, desc.depthStencilState.frontFace);
                    ApplyMetalStencilFaceDesc(depthStencilDescriptor.backFaceStencil, desc.depthStencilState.backFace);
                }

                id<MTLDepthStencilState> depthStencilState = [device_ newDepthStencilStateWithDescriptor:depthStencilDescriptor];
                [depthStencilDescriptor release];

                if (depthStencilState == nil)
                {
                    SetLastError("MTLDevice newDepthStencilStateWithDescriptor failed.");
                    [pipelineState release];
                    return nullptr;
                }

                std::vector<RhiPipelineResourceBindingDesc> resourceBindings;
                if (desc.resourceLayout.bindingCount != 0)
                {
                    resourceBindings.assign(desc.resourceLayout.bindings, desc.resourceLayout.bindings + desc.resourceLayout.bindingCount);
                }
                return std::make_unique<MetalPipelineState>(
                    desc.primitiveType, desc.rasterizerState, pipelineState, depthStencilState, std::move(resourceBindings));
            }

            [[nodiscard]] std::unique_ptr<RhiCommandList> CreateCommandList() override
            {
                return std::make_unique<MetalCommandList>(commandQueue_);
            }

            [[nodiscard]] std::unique_ptr<RhiFence> CreateFence(uint64_t initialValue = 0) override
            {
                return std::make_unique<MetalFence>(initialValue);
            }

            [[nodiscard]] bool Submit(RhiCommandList& commandList, RhiFence* completionFence, uint64_t completionValue) override
            {
                auto* metalCommandList = dynamic_cast<MetalCommandList*>(&commandList);

                if (metalCommandList == nullptr)
                {
                    SetLastError("Metal device can only submit Metal command lists.");
                    return false;
                }

                MetalFence* metalFence = nullptr;
                if (completionFence != nullptr)
                {
                    metalFence = dynamic_cast<MetalFence*>(completionFence);
                    if (metalFence == nullptr)
                    {
                        SetLastError("Metal device can only signal Metal fences.");
                        return false;
                    }
                }

                return metalCommandList->Commit(metalFence, completionValue);
            }

            void WaitIdle() override
            {
                @autoreleasepool
                {
                    id<MTLCommandBuffer> commandBuffer = [commandQueue_ commandBuffer];
                    [commandBuffer commit];
                    [commandBuffer waitUntilCompleted];
                }
            }

            [[nodiscard]] void* GetNativeDeviceHandle() const noexcept override
            {
                return device_;
            }

            [[nodiscard]] void* GetNativeGraphicsQueueHandle() const noexcept override
            {
                return commandQueue_;
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
