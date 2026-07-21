#include "Engine/RHI/D3D11/D3D11Rhi.h"

#include "Engine/RHI/Common/RhiUtils.h"
#include "Engine/Runtime/Core/Assert.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace ve::rhi
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        DXGI_FORMAT ToDxgiFormat(RhiFormat format)
        {
            switch (format)
            {
            case RhiFormat::Rgba8Unorm:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case RhiFormat::Bgra8Unorm:
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            case RhiFormat::Rgb32Float:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case RhiFormat::Depth32Float:
                return DXGI_FORMAT_D32_FLOAT;
            case RhiFormat::Unknown:
            default:
                return DXGI_FORMAT_UNKNOWN;
            }
        }

        [[nodiscard]] bool HasTextureUsage(RhiTextureUsage usage, RhiTextureUsage flag) noexcept
        {
            return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(flag)) != 0;
        }

        [[nodiscard]] bool IsD3D11TextureDescSupported(const RhiTextureDesc& desc) noexcept
        {
            const bool sampled = HasTextureUsage(desc.usage, RhiTextureUsage::Sampled);
            const bool depthStencil = HasTextureUsage(desc.usage, RhiTextureUsage::DepthStencil);
            const bool renderTarget = HasTextureUsage(desc.usage, RhiTextureUsage::RenderTarget);
            if (depthStencil)
            {
                return desc.format == RhiFormat::Depth32Float && !renderTarget;
            }

            return desc.format != RhiFormat::Depth32Float || (!sampled && !renderTarget);
        }

        [[nodiscard]] DXGI_FORMAT ToD3D11TextureResourceFormat(const RhiTextureDesc& desc) noexcept
        {
            if (desc.format == RhiFormat::Depth32Float && HasTextureUsage(desc.usage, RhiTextureUsage::Sampled) &&
                HasTextureUsage(desc.usage, RhiTextureUsage::DepthStencil))
            {
                return DXGI_FORMAT_R32_TYPELESS;
            }

            return ToDxgiFormat(desc.format);
        }

        D3D11_PRIMITIVE_TOPOLOGY ToD3D11Topology(RhiPrimitiveTopology topology)
        {
            switch (topology)
            {
            case RhiPrimitiveTopology::LineList:
                return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            case RhiPrimitiveTopology::TriangleList:
            default:
                return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            }
        }

        D3D11_FILTER ToD3D11Filter(RhiSamplerFilter filter, RhiSamplerReductionMode reductionMode)
        {
            const bool comparison = reductionMode == RhiSamplerReductionMode::Comparison;
            switch (filter)
            {
            case RhiSamplerFilter::Point:
                return comparison ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_POINT;
            case RhiSamplerFilter::Trilinear:
                return comparison ? D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            case RhiSamplerFilter::Anisotropic:
                return comparison ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
            case RhiSamplerFilter::Bilinear:
            default:
                return comparison ? D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            }
        }

        D3D11_TEXTURE_ADDRESS_MODE ToD3D11AddressMode(RhiSamplerAddressMode mode)
        {
            switch (mode)
            {
            case RhiSamplerAddressMode::Mirror:
                return D3D11_TEXTURE_ADDRESS_MIRROR;
            case RhiSamplerAddressMode::Clamp:
                return D3D11_TEXTURE_ADDRESS_CLAMP;
            case RhiSamplerAddressMode::Border:
                return D3D11_TEXTURE_ADDRESS_BORDER;
            case RhiSamplerAddressMode::Wrap:
            default:
                return D3D11_TEXTURE_ADDRESS_WRAP;
            }
        }

        D3D11_CULL_MODE ToD3D11CullMode(RhiCullMode cullMode)
        {
            switch (cullMode)
            {
            case RhiCullMode::None:
                return D3D11_CULL_NONE;
            case RhiCullMode::Front:
                return D3D11_CULL_FRONT;
            case RhiCullMode::Back:
            default:
                return D3D11_CULL_BACK;
            }
        }

        D3D11_COMPARISON_FUNC ToD3D11ComparisonFunc(RhiCompareFunction compareFunction)
        {
            switch (compareFunction)
            {
            case RhiCompareFunction::Never:
                return D3D11_COMPARISON_NEVER;
            case RhiCompareFunction::Less:
                return D3D11_COMPARISON_LESS;
            case RhiCompareFunction::Equal:
                return D3D11_COMPARISON_EQUAL;
            case RhiCompareFunction::Always:
                return D3D11_COMPARISON_ALWAYS;
            case RhiCompareFunction::LessEqual:
                return D3D11_COMPARISON_LESS_EQUAL;
            case RhiCompareFunction::Greater:
                return D3D11_COMPARISON_GREATER;
            case RhiCompareFunction::NotEqual:
                return D3D11_COMPARISON_NOT_EQUAL;
            case RhiCompareFunction::GreaterEqual:
            default:
                return D3D11_COMPARISON_GREATER_EQUAL;
            }
        }

        D3D11_BLEND ToD3D11Blend(RhiBlendFactor factor)
        {
            switch (factor)
            {
            case RhiBlendFactor::Zero:
                return D3D11_BLEND_ZERO;
            case RhiBlendFactor::One:
                return D3D11_BLEND_ONE;
            case RhiBlendFactor::SourceColor:
                return D3D11_BLEND_SRC_COLOR;
            case RhiBlendFactor::OneMinusSourceColor:
                return D3D11_BLEND_INV_SRC_COLOR;
            case RhiBlendFactor::SourceAlpha:
                return D3D11_BLEND_SRC_ALPHA;
            case RhiBlendFactor::OneMinusSourceAlpha:
                return D3D11_BLEND_INV_SRC_ALPHA;
            case RhiBlendFactor::DestinationColor:
                return D3D11_BLEND_DEST_COLOR;
            case RhiBlendFactor::OneMinusDestinationColor:
                return D3D11_BLEND_INV_DEST_COLOR;
            case RhiBlendFactor::DestinationAlpha:
                return D3D11_BLEND_DEST_ALPHA;
            case RhiBlendFactor::OneMinusDestinationAlpha:
            default:
                return D3D11_BLEND_INV_DEST_ALPHA;
            }
        }

        D3D11_BLEND_OP ToD3D11BlendOp(RhiBlendOperation operation)
        {
            switch (operation)
            {
            case RhiBlendOperation::Add:
                return D3D11_BLEND_OP_ADD;
            case RhiBlendOperation::Subtract:
                return D3D11_BLEND_OP_SUBTRACT;
            case RhiBlendOperation::ReverseSubtract:
                return D3D11_BLEND_OP_REV_SUBTRACT;
            case RhiBlendOperation::Min:
                return D3D11_BLEND_OP_MIN;
            case RhiBlendOperation::Max:
            default:
                return D3D11_BLEND_OP_MAX;
            }
        }

        D3D11_STENCIL_OP ToD3D11StencilOp(RhiStencilOperation operation)
        {
            switch (operation)
            {
            case RhiStencilOperation::Keep:
                return D3D11_STENCIL_OP_KEEP;
            case RhiStencilOperation::Zero:
                return D3D11_STENCIL_OP_ZERO;
            case RhiStencilOperation::Replace:
                return D3D11_STENCIL_OP_REPLACE;
            case RhiStencilOperation::IncrementClamp:
                return D3D11_STENCIL_OP_INCR_SAT;
            case RhiStencilOperation::DecrementClamp:
                return D3D11_STENCIL_OP_DECR_SAT;
            case RhiStencilOperation::Invert:
                return D3D11_STENCIL_OP_INVERT;
            case RhiStencilOperation::IncrementWrap:
                return D3D11_STENCIL_OP_INCR;
            case RhiStencilOperation::DecrementWrap:
            default:
                return D3D11_STENCIL_OP_DECR;
            }
        }

        D3D11_DEPTH_STENCILOP_DESC ToD3D11StencilFaceDesc(const RhiStencilFaceDesc& desc)
        {
            D3D11_DEPTH_STENCILOP_DESC d3dDesc = {};
            d3dDesc.StencilFailOp = ToD3D11StencilOp(desc.failOperation);
            d3dDesc.StencilDepthFailOp = ToD3D11StencilOp(desc.depthFailOperation);
            d3dDesc.StencilPassOp = ToD3D11StencilOp(desc.passOperation);
            d3dDesc.StencilFunc = ToD3D11ComparisonFunc(desc.compareFunction);
            return d3dDesc;
        }

        UINT8 ToD3D11ColorWriteMask(uint8_t mask)
        {
            UINT8 d3dMask = 0;
            if ((mask & RhiColorWriteRed) != 0)
            {
                d3dMask |= D3D11_COLOR_WRITE_ENABLE_RED;
            }
            if ((mask & RhiColorWriteGreen) != 0)
            {
                d3dMask |= D3D11_COLOR_WRITE_ENABLE_GREEN;
            }
            if ((mask & RhiColorWriteBlue) != 0)
            {
                d3dMask |= D3D11_COLOR_WRITE_ENABLE_BLUE;
            }
            if ((mask & RhiColorWriteAlpha) != 0)
            {
                d3dMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
            }
            return d3dMask;
        }

        UINT ToD3D11TextureBindFlags(RhiTextureUsage usage)
        {
            UINT flags = 0;
            const auto usageValue = static_cast<uint32_t>(usage);

            if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::Sampled)) != 0)
            {
                flags |= D3D11_BIND_SHADER_RESOURCE;
            }

            if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::RenderTarget)) != 0)
            {
                flags |= D3D11_BIND_RENDER_TARGET;
            }

            if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::DepthStencil)) != 0)
            {
                flags |= D3D11_BIND_DEPTH_STENCIL;
            }

            return flags;
        }

        UINT ToD3D11BufferBindFlags(RhiBufferUsage usage)
        {
            switch (usage)
            {
            case RhiBufferUsage::Vertex:
                return D3D11_BIND_VERTEX_BUFFER;
            case RhiBufferUsage::Index:
                return D3D11_BIND_INDEX_BUFFER;
            case RhiBufferUsage::Uniform:
                return D3D11_BIND_CONSTANT_BUFFER;
            }

            return D3D11_BIND_VERTEX_BUFFER;
        }

        DXGI_FORMAT ToDxgiIndexFormat(RhiIndexFormat format)
        {
            switch (format)
            {
            case RhiIndexFormat::UInt16:
                return DXGI_FORMAT_R16_UINT;
            case RhiIndexFormat::UInt32:
                return DXGI_FORMAT_R32_UINT;
            }

            return DXGI_FORMAT_R32_UINT;
        }

        std::string MakeHResultError(const char* operation, HRESULT result)
        {
            char buffer[128] = {};
            std::snprintf(buffer, sizeof(buffer), "%s failed with HRESULT 0x%08X", operation, static_cast<unsigned>(result));
            return buffer;
        }

        class D3D11Buffer final : public RhiBuffer
        {
        public:
            D3D11Buffer(ComPtr<ID3D11Buffer> buffer, uint64_t size, RhiBufferMemoryUsage memoryUsage)
                : buffer_(std::move(buffer))
                , size_(size)
                , memoryUsage_(memoryUsage)
            {
            }

            [[nodiscard]] uint64_t GetSize() const noexcept override
            {
                return size_;
            }

            [[nodiscard]] ID3D11Buffer* GetNativeBuffer() const noexcept
            {
                return buffer_.Get();
            }

            [[nodiscard]] RhiBufferMemoryUsage GetMemoryUsage() const noexcept
            {
                return memoryUsage_;
            }

        private:
            ComPtr<ID3D11Buffer> buffer_;
            uint64_t size_ = 0;
            RhiBufferMemoryUsage memoryUsage_ = RhiBufferMemoryUsage::GpuOnly;
        };

        class D3D11Texture final : public RhiTexture
        {
        public:
            D3D11Texture(ComPtr<ID3D11Texture2D> texture,
                         ComPtr<ID3D11RenderTargetView> renderTargetView,
                         ComPtr<ID3D11DepthStencilView> depthStencilView,
                         ComPtr<ID3D11ShaderResourceView> shaderResourceView,
                         RhiTextureDesc desc)
                : texture_(std::move(texture))
                , renderTargetView_(std::move(renderTargetView))
                , depthStencilView_(std::move(depthStencilView))
                , shaderResourceView_(std::move(shaderResourceView))
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

            [[nodiscard]] ID3D11Texture2D* GetNativeTexture() const noexcept
            {
                return texture_.Get();
            }

            [[nodiscard]] ID3D11RenderTargetView* GetRenderTargetView() const noexcept
            {
                return renderTargetView_.Get();
            }

            [[nodiscard]] ID3D11DepthStencilView* GetDepthStencilView() const noexcept
            {
                return depthStencilView_.Get();
            }

            [[nodiscard]] ID3D11ShaderResourceView* GetShaderResourceView() const noexcept
            {
                return shaderResourceView_.Get();
            }

            [[nodiscard]] void* GetNativeSampledViewHandle() const noexcept override
            {
                return shaderResourceView_.Get();
            }

        private:
            ComPtr<ID3D11Texture2D> texture_;
            ComPtr<ID3D11RenderTargetView> renderTargetView_;
            ComPtr<ID3D11DepthStencilView> depthStencilView_;
            ComPtr<ID3D11ShaderResourceView> shaderResourceView_;
            RhiTextureDesc desc_ = {};
        };

        class D3D11Sampler final : public RhiSampler
        {
        public:
            D3D11Sampler(ComPtr<ID3D11SamplerState> samplerState, RhiSamplerDesc desc)
                : samplerState_(std::move(samplerState))
                , desc_(desc)
            {
            }

            [[nodiscard]] RhiSamplerFilter GetFilter() const noexcept override
            {
                return desc_.filter;
            }

            [[nodiscard]] ID3D11SamplerState* GetNativeSamplerState() const noexcept
            {
                return samplerState_.Get();
            }

        private:
            ComPtr<ID3D11SamplerState> samplerState_;
            RhiSamplerDesc desc_ = {};
        };

        class D3D11Fence final : public RhiFence
        {
        public:
            D3D11Fence(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> context, uint64_t initialValue)
                : device_(std::move(device))
                , context_(std::move(context))
                , signaledValue_(initialValue)
                , completedValue_(initialValue)
            {
            }

            [[nodiscard]] bool Signal(uint64_t value)
            {
                D3D11_QUERY_DESC queryDesc = {};
                queryDesc.Query = D3D11_QUERY_EVENT;

                ComPtr<ID3D11Query> query;
                HRESULT result = device_->CreateQuery(&queryDesc, &query);

                if (FAILED(result))
                {
                    return false;
                }

                context_->End(query.Get());
                query_ = std::move(query);
                signaledValue_ = value;
                return true;
            }

            [[nodiscard]] bool IsComplete(uint64_t value) const noexcept override
            {
                if (value <= completedValue_)
                {
                    return true;
                }

                if (value > signaledValue_ || query_ == nullptr || context_ == nullptr)
                {
                    return false;
                }

                const HRESULT result = context_->GetData(query_.Get(), nullptr, 0, 0);
                if (result == S_OK)
                {
                    completedValue_ = signaledValue_;
                    return value <= completedValue_;
                }

                return false;
            }

            [[nodiscard]] bool Wait(uint64_t value) override
            {
                while (!IsComplete(value))
                {
                    Sleep(0);
                }

                return true;
            }

            [[nodiscard]] uint64_t GetCompletedValue() const noexcept override
            {
                static_cast<void>(IsComplete(signaledValue_));
                return completedValue_;
            }

        private:
            ComPtr<ID3D11Device> device_;
            ComPtr<ID3D11DeviceContext> context_;
            ComPtr<ID3D11Query> query_;
            uint64_t signaledValue_ = 0;
            mutable uint64_t completedValue_ = 0;
        };

        class D3D11ShaderModule final : public RhiShaderModule
        {
        public:
            D3D11ShaderModule(RhiShaderStage stage, ComPtr<ID3DBlob> bytecode)
                : stage_(stage)
                , bytecode_(std::move(bytecode))
            {
            }

            [[nodiscard]] RhiShaderStage GetStage() const noexcept override
            {
                return stage_;
            }

            [[nodiscard]] ID3DBlob* GetBytecode() const noexcept
            {
                return bytecode_.Get();
            }

        private:
            RhiShaderStage stage_ = RhiShaderStage::Vertex;
            ComPtr<ID3DBlob> bytecode_;
        };

        class D3D11PipelineState final : public RhiPipelineState
        {
        public:
            D3D11PipelineState(RhiPrimitiveTopology topology,
                               ComPtr<ID3D11VertexShader> vertexShader,
                               ComPtr<ID3D11PixelShader> pixelShader,
                               ComPtr<ID3D11InputLayout> inputLayout,
                               ComPtr<ID3D11RasterizerState> rasterizerState,
                               ComPtr<ID3D11DepthStencilState> depthStencilState,
                               ComPtr<ID3D11BlendState> blendState,
                               std::vector<RhiPipelineResourceBindingDesc> resourceBindings)
                : topology_(topology)
                , vertexShader_(std::move(vertexShader))
                , pixelShader_(std::move(pixelShader))
                , inputLayout_(std::move(inputLayout))
                , rasterizerState_(std::move(rasterizerState))
                , depthStencilState_(std::move(depthStencilState))
                , blendState_(std::move(blendState))
                , resourceBindings_(std::move(resourceBindings))
            {
            }

            [[nodiscard]] RhiPrimitiveTopology GetTopology() const noexcept override
            {
                return topology_;
            }

            void Bind(ID3D11DeviceContext* context) const
            {
                context->IASetPrimitiveTopology(ToD3D11Topology(topology_));
                context->IASetInputLayout(inputLayout_.Get());
                context->RSSetState(rasterizerState_.Get());
                context->OMSetDepthStencilState(depthStencilState_.Get(), 0);
                const float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                context->OMSetBlendState(blendState_.Get(), blendFactor, 0xFFFFFFFFu);
                context->VSSetShader(vertexShader_.Get(), nullptr, 0);
                context->PSSetShader(pixelShader_.Get(), nullptr, 0);
            }

            [[nodiscard]] bool HasBinding(RhiPipelineResourceKind kind, RhiShaderStage stage, uint32_t slot) const noexcept
            {
                return std::any_of(resourceBindings_.begin(),
                                   resourceBindings_.end(),
                                   [kind, stage, slot](const RhiPipelineResourceBindingDesc& binding)
                                   { return binding.kind == kind && binding.stage == stage && binding.slot == slot; });
            }

        private:
            RhiPrimitiveTopology topology_ = RhiPrimitiveTopology::TriangleList;
            ComPtr<ID3D11VertexShader> vertexShader_;
            ComPtr<ID3D11PixelShader> pixelShader_;
            ComPtr<ID3D11InputLayout> inputLayout_;
            ComPtr<ID3D11RasterizerState> rasterizerState_;
            ComPtr<ID3D11DepthStencilState> depthStencilState_;
            ComPtr<ID3D11BlendState> blendState_;
            std::vector<RhiPipelineResourceBindingDesc> resourceBindings_;
        };

        class D3D11Swapchain final : public RhiSwapchain
        {
        public:
            D3D11Swapchain(ComPtr<ID3D11Device> device,
                           ComPtr<IDXGISwapChain> swapchain,
                           RhiExtent2D extent,
                           RhiFormat colorFormat,
                           uint32_t bufferCount,
                           std::string* lastError)
                : device_(std::move(device))
                , swapchain_(std::move(swapchain))
                , extent_(extent)
                , colorFormat_(colorFormat)
                , bufferCount_(bufferCount)
                , lastError_(lastError)
            {
            }

            [[nodiscard]] bool Initialize()
            {
                return CreateBackBufferView();
            }

            [[nodiscard]] bool Resize(RhiExtent2D extent) override
            {
                if (extent.width == 0 || extent.height == 0)
                {
                    SetLastError("D3D11 swapchain resize requires a non-empty extent.");
                    return false;
                }

                if (extent.width == extent_.width && extent.height == extent_.height)
                {
                    return true;
                }

                ComPtr<ID3D11DeviceContext> context;
                device_->GetImmediateContext(&context);
                if (context != nullptr)
                {
                    context->OMSetRenderTargets(0, nullptr, nullptr);
                    context->Flush();
                }

                renderTargetView_.Reset();
                backBuffer_.Reset();

                const HRESULT result = swapchain_->ResizeBuffers(bufferCount_, extent.width, extent.height, ToDxgiFormat(colorFormat_), 0);
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("IDXGISwapChain::ResizeBuffers", result));
                    const bool restored = CreateBackBufferView();
                    static_cast<void>(restored);
                    return false;
                }

                extent_ = extent;
                return CreateBackBufferView();
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
                return bufferCount_;
            }

            [[nodiscard]] bool Present() override
            {
                HRESULT result = swapchain_->Present(1, 0);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("IDXGISwapChain::Present", result));
                    return false;
                }

                return true;
            }

            [[nodiscard]] ID3D11RenderTargetView* GetRenderTargetView() const noexcept
            {
                return renderTargetView_.Get();
            }

            [[nodiscard]] ID3D11Texture2D* GetCurrentBackBuffer() const noexcept
            {
                return backBuffer_.Get();
            }

        private:
            [[nodiscard]] bool CreateBackBufferView()
            {
                HRESULT result = swapchain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer_));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("IDXGISwapChain::GetBuffer", result));
                    return false;
                }

                result = device_->CreateRenderTargetView(backBuffer_.Get(), nullptr, &renderTargetView_);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateRenderTargetView", result));
                    return false;
                }

                return true;
            }
            void SetLastError(std::string error)
            {
                if (lastError_ != nullptr)
                {
                    *lastError_ = std::move(error);
                }
            }

        private:
            ComPtr<ID3D11Device> device_;
            ComPtr<IDXGISwapChain> swapchain_;
            ComPtr<ID3D11Texture2D> backBuffer_;
            ComPtr<ID3D11RenderTargetView> renderTargetView_;
            RhiExtent2D extent_ = {};
            RhiFormat colorFormat_ = RhiFormat::Bgra8Unorm;
            uint32_t bufferCount_ = 2;
            std::string* lastError_ = nullptr;
        };

        class D3D11CommandList final : public RhiCommandList
        {
        public:
            D3D11CommandList(ComPtr<ID3D11DeviceContext> context, ComPtr<ID3D11DeviceContext1> context1)
                : context_(std::move(context))
                , context1_(std::move(context1))
            {
            }

            [[nodiscard]] bool Begin() override
            {
                activePipeline_ = nullptr;
                activeRenderTargetView_ = nullptr;
                activeDepthTexture_ = nullptr;
                return true;
            }

            [[nodiscard]] bool End() override
            {
                return true;
            }

            [[nodiscard]] bool BeginRenderPass(RhiSwapchain& swapchain, const RhiRenderPassBeginInfo& beginInfo) override
            {
                if ((!beginInfo.hasColorAttachment && beginInfo.colorAttachmentIsSwapchain) ||
                    (!beginInfo.hasColorAttachment && beginInfo.colorAttachment.texture != nullptr) ||
                    (!beginInfo.hasColorAttachment && !beginInfo.hasDepthAttachment))
                {
                    return false;
                }

                const RhiRenderPassColorAttachmentInfo& colorAttachment = beginInfo.colorAttachment;
                ID3D11RenderTargetView* renderTargetView = nullptr;
                ID3D11DepthStencilView* depthStencilView = nullptr;
                if (beginInfo.hasColorAttachment && beginInfo.colorAttachmentIsSwapchain)
                {
                    if (colorAttachment.texture != nullptr)
                    {
                        return false;
                    }

                    auto* d3dSwapchain = dynamic_cast<D3D11Swapchain*>(&swapchain);
                    if (d3dSwapchain == nullptr)
                    {
                        return false;
                    }

                    renderTargetView = d3dSwapchain->GetRenderTargetView();
                }
                else if (beginInfo.hasColorAttachment)
                {
                    auto* d3dTexture = dynamic_cast<D3D11Texture*>(colorAttachment.texture);
                    if (d3dTexture == nullptr)
                    {
                        return false;
                    }

                    renderTargetView = d3dTexture->GetRenderTargetView();
                }

                if (beginInfo.hasColorAttachment && renderTargetView == nullptr)
                {
                    return false;
                }

                if (beginInfo.hasDepthAttachment)
                {
                    auto* d3dDepthTexture = dynamic_cast<D3D11Texture*>(beginInfo.depthAttachment.texture);
                    if (d3dDepthTexture == nullptr)
                    {
                        return false;
                    }

                    depthStencilView = d3dDepthTexture->GetDepthStencilView();
                    if (depthStencilView == nullptr)
                    {
                        return false;
                    }

                    ClearShaderResourceBindings();
                    activeDepthTexture_ = d3dDepthTexture;
                }

                activeRenderTargetView_ = renderTargetView;
                context_->OMSetRenderTargets(
                    beginInfo.hasColorAttachment ? 1u : 0u, beginInfo.hasColorAttachment ? &renderTargetView : nullptr, depthStencilView);

                if (beginInfo.hasColorAttachment && colorAttachment.loadAction == RhiLoadAction::Clear)
                {
                    const float clearColor[4] = {
                        colorAttachment.clearColor.r, colorAttachment.clearColor.g, colorAttachment.clearColor.b, colorAttachment.clearColor.a};
                    context_->ClearRenderTargetView(renderTargetView, clearColor);
                }

                if (depthStencilView != nullptr && beginInfo.depthAttachment.loadAction == RhiLoadAction::Clear)
                {
                    context_->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH, beginInfo.depthAttachment.clearDepth, 0);
                }

                return true;
            }

            void EndRenderPass() override
            {
                context_->OMSetRenderTargets(0, nullptr, nullptr);
                activeRenderTargetView_ = nullptr;
                activeDepthTexture_ = nullptr;
            }

            [[nodiscard]] bool CopyTextureToSwapchain(RhiTexture& sourceTexture, RhiSwapchain& swapchain) override
            {
                auto* d3dTexture = dynamic_cast<D3D11Texture*>(&sourceTexture);
                auto* d3dSwapchain = dynamic_cast<D3D11Swapchain*>(&swapchain);
                if (d3dTexture == nullptr || d3dSwapchain == nullptr)
                {
                    return false;
                }

                const RhiExtent2D swapchainExtent = d3dSwapchain->GetExtent();
                if (sourceTexture.GetWidth() != swapchainExtent.width || sourceTexture.GetHeight() != swapchainExtent.height ||
                    sourceTexture.GetFormat() != d3dSwapchain->GetColorFormat())
                {
                    return false;
                }

                ID3D11RenderTargetView* nullRenderTarget = nullptr;
                context_->OMSetRenderTargets(1, &nullRenderTarget, nullptr);
                context_->CopyResource(d3dSwapchain->GetCurrentBackBuffer(), d3dTexture->GetNativeTexture());
                return true;
            }

            void SetPipeline(const RhiPipelineState& pipelineState) override
            {
                const auto& d3dPipelineState = static_cast<const D3D11PipelineState&>(pipelineState);
                activePipeline_ = &d3dPipelineState;
                d3dPipelineState.Bind(context_.Get());
            }

            void SetViewport(const RhiViewport& viewport) override
            {
                D3D11_VIEWPORT d3dViewport = {};
                d3dViewport.TopLeftX = viewport.x;
                d3dViewport.TopLeftY = viewport.y;
                d3dViewport.Width = viewport.width;
                d3dViewport.Height = viewport.height;
                d3dViewport.MinDepth = viewport.minDepth;
                d3dViewport.MaxDepth = viewport.maxDepth;
                context_->RSSetViewports(1, &d3dViewport);
            }

            void SetScissor(const RhiScissorRect& scissorRect) override
            {
                D3D11_RECT d3dRect = {};
                d3dRect.left = scissorRect.x;
                d3dRect.top = scissorRect.y;
                d3dRect.right = scissorRect.x + static_cast<LONG>(scissorRect.width);
                d3dRect.bottom = scissorRect.y + static_cast<LONG>(scissorRect.height);
                context_->RSSetScissorRects(1, &d3dRect);
            }

            void SetVertexBuffer(uint32_t slot, const RhiBuffer& buffer, uint32_t stride, uint64_t offset) override
            {
                const auto& d3dBuffer = static_cast<const D3D11Buffer&>(buffer);
                ID3D11Buffer* nativeBuffer = d3dBuffer.GetNativeBuffer();
                const UINT d3dStride = stride;
                const UINT d3dOffset = static_cast<UINT>(offset);
                context_->IASetVertexBuffers(slot, 1, &nativeBuffer, &d3dStride, &d3dOffset);
            }

            void SetIndexBuffer(const RhiBuffer& buffer, RhiIndexFormat format, uint64_t offset) override
            {
                const auto& d3dBuffer = static_cast<const D3D11Buffer&>(buffer);
                context_->IASetIndexBuffer(d3dBuffer.GetNativeBuffer(), ToDxgiIndexFormat(format), static_cast<UINT>(offset));
            }

            void SetUniformBuffer(RhiShaderStage stage, uint32_t slot, const RhiBuffer& buffer, uint64_t offset, uint64_t size) override
            {
                if (!ValidateBinding(RhiPipelineResourceKind::UniformBuffer, stage, slot))
                {
                    return;
                }

                constexpr uint64_t ConstantSize = 16;
                constexpr uint64_t BindingAlignment = 16 * ConstantSize;
                VE_ASSERT(context1_ != nullptr);
                VE_ASSERT(size > 0);
                VE_ASSERT(offset % BindingAlignment == 0);
                VE_ASSERT(offset + size <= buffer.GetSize());
                const auto& d3dBuffer = static_cast<const D3D11Buffer&>(buffer);
                ID3D11Buffer* nativeBuffer = d3dBuffer.GetNativeBuffer();
                const UINT firstConstant = static_cast<UINT>(offset / ConstantSize);
                const UINT constantCount = static_cast<UINT>(((size + BindingAlignment - 1) / BindingAlignment) * (BindingAlignment / ConstantSize));
                switch (stage)
                {
                case RhiShaderStage::Vertex:
                    context1_->VSSetConstantBuffers1(slot, 1, &nativeBuffer, &firstConstant, &constantCount);
                    break;
                case RhiShaderStage::Fragment:
                    context1_->PSSetConstantBuffers1(slot, 1, &nativeBuffer, &firstConstant, &constantCount);
                    break;
                }
            }

            void SetTexture(RhiShaderStage stage, uint32_t slot, const RhiTexture& texture) override
            {
                if (!ValidateBinding(RhiPipelineResourceKind::SampledTexture, stage, slot))
                {
                    return;
                }

                const auto& d3dTexture = static_cast<const D3D11Texture&>(texture);
                if (&d3dTexture == activeDepthTexture_)
                {
                    context_->OMSetRenderTargets(
                        activeRenderTargetView_ != nullptr ? 1u : 0u, activeRenderTargetView_ != nullptr ? &activeRenderTargetView_ : nullptr, nullptr);
                    activeDepthTexture_ = nullptr;
                }
                ID3D11ShaderResourceView* shaderResourceView = d3dTexture.GetShaderResourceView();
                VE_ASSERT_MESSAGE(shaderResourceView != nullptr, "D3D11 sampled texture binding requires a shader-resource view.");
                if (shaderResourceView == nullptr)
                {
                    return;
                }
                switch (stage)
                {
                case RhiShaderStage::Vertex:
                    context_->VSSetShaderResources(slot, 1, &shaderResourceView);
                    break;
                case RhiShaderStage::Fragment:
                    context_->PSSetShaderResources(slot, 1, &shaderResourceView);
                    break;
                }
            }

            void SetSampler(RhiShaderStage stage, uint32_t slot, const RhiSampler& sampler) override
            {
                if (!ValidateBinding(RhiPipelineResourceKind::Sampler, stage, slot))
                {
                    return;
                }

                const auto& d3dSampler = static_cast<const D3D11Sampler&>(sampler);
                ID3D11SamplerState* samplerState = d3dSampler.GetNativeSamplerState();
                switch (stage)
                {
                case RhiShaderStage::Vertex:
                    context_->VSSetSamplers(slot, 1, &samplerState);
                    break;
                case RhiShaderStage::Fragment:
                    context_->PSSetSamplers(slot, 1, &samplerState);
                    break;
                }
            }

            void Draw(uint32_t vertexCount, uint32_t firstVertex) override
            {
                context_->Draw(vertexCount, firstVertex);
            }

            void DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) override
            {
                context_->DrawIndexed(indexCount, firstIndex, vertexOffset);
            }

        private:
            [[nodiscard]] bool ValidateBinding(RhiPipelineResourceKind kind, RhiShaderStage stage, uint32_t slot) const noexcept
            {
                const bool valid = activePipeline_ != nullptr && activePipeline_->HasBinding(kind, stage, slot);
                VE_ASSERT_MESSAGE(valid, "D3D11 resource binding is absent from the active pipeline layout.");
                return valid;
            }

            void ClearShaderResourceBindings()
            {
                ID3D11ShaderResourceView* nullViews[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
                context_->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullViews);
                context_->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullViews);
            }

            ComPtr<ID3D11DeviceContext> context_;
            ComPtr<ID3D11DeviceContext1> context1_;
            const D3D11PipelineState* activePipeline_ = nullptr;
            ID3D11RenderTargetView* activeRenderTargetView_ = nullptr;
            const D3D11Texture* activeDepthTexture_ = nullptr;
        };

        class D3D11Device final : public RhiDevice
        {
        public:
            explicit D3D11Device(bool enableDebug)
                : enableDebug_(enableDebug)
            {
            }

            [[nodiscard]] bool Initialize()
            {
                UINT flags = 0;

                if (enableDebug_)
                {
                    flags |= D3D11_CREATE_DEVICE_DEBUG;
                }

                const D3D_FEATURE_LEVEL requestedFeatureLevels[] = {
                    D3D_FEATURE_LEVEL_11_1,
                    D3D_FEATURE_LEVEL_11_0,
                };

                D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
                HRESULT result = D3D11CreateDevice(nullptr,
                                                   D3D_DRIVER_TYPE_HARDWARE,
                                                   nullptr,
                                                   flags,
                                                   requestedFeatureLevels,
                                                   static_cast<UINT>(std::size(requestedFeatureLevels)),
                                                   D3D11_SDK_VERSION,
                                                   &device_,
                                                   &featureLevel,
                                                   &context_);

                if (FAILED(result) && enableDebug_)
                {
                    flags &= ~D3D11_CREATE_DEVICE_DEBUG;
                    result = D3D11CreateDevice(nullptr,
                                               D3D_DRIVER_TYPE_HARDWARE,
                                               nullptr,
                                               flags,
                                               requestedFeatureLevels,
                                               static_cast<UINT>(std::size(requestedFeatureLevels)),
                                               D3D11_SDK_VERSION,
                                               &device_,
                                               &featureLevel,
                                               &context_);
                }

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("D3D11CreateDevice", result));
                    return false;
                }

                result = context_.As(&context1_);
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11DeviceContext::QueryInterface ID3D11DeviceContext1", result));
                    return false;
                }

                D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
                result = device_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options));
                if (FAILED(result) || !options.MapNoOverwriteOnDynamicConstantBuffer)
                {
                    SetLastError("D3D11.1 dynamic constant-buffer no-overwrite updates are required.");
                    return false;
                }

                return true;
            }

            [[nodiscard]] RhiBackend GetBackend() const noexcept override
            {
                return RhiBackend::D3D11;
            }

            [[nodiscard]] const char* GetLastErrorMessage() const noexcept override
            {
                return lastError_.c_str();
            }

            [[nodiscard]] void* GetNativeDeviceHandle() const noexcept override
            {
                return device_.Get();
            }

            [[nodiscard]] void* GetNativeImmediateContextHandle() const noexcept override
            {
                return context_.Get();
            }

            [[nodiscard]] std::unique_ptr<RhiSwapchain> CreateSwapchain(const RhiSwapchainDesc& desc) override
            {
                HWND window = static_cast<HWND>(desc.nativeWindow);

                if (window == nullptr)
                {
                    SetLastError("D3D11 swapchain requires a native HWND.");
                    return nullptr;
                }

                ComPtr<IDXGIDevice> dxgiDevice;
                HRESULT result = device_.As(&dxgiDevice);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::QueryInterface IDXGIDevice", result));
                    return nullptr;
                }

                ComPtr<IDXGIAdapter> adapter;
                result = dxgiDevice->GetAdapter(&adapter);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("IDXGIDevice::GetAdapter", result));
                    return nullptr;
                }

                ComPtr<IDXGIFactory> factory;
                result = adapter->GetParent(IID_PPV_ARGS(&factory));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("IDXGIAdapter::GetParent", result));
                    return nullptr;
                }

                DXGI_SWAP_CHAIN_DESC swapchainDesc = {};
                swapchainDesc.BufferDesc.Width = desc.width;
                swapchainDesc.BufferDesc.Height = desc.height;
                swapchainDesc.BufferDesc.Format = ToDxgiFormat(desc.colorFormat);
                swapchainDesc.BufferDesc.RefreshRate.Numerator = 60;
                swapchainDesc.BufferDesc.RefreshRate.Denominator = 1;
                swapchainDesc.SampleDesc.Count = 1;
                swapchainDesc.SampleDesc.Quality = 0;
                swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                swapchainDesc.BufferCount = std::max(desc.bufferCount, 1u);
                swapchainDesc.OutputWindow = window;
                swapchainDesc.Windowed = TRUE;
                swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

                ComPtr<IDXGISwapChain> swapchain;
                result = factory->CreateSwapChain(device_.Get(), &swapchainDesc, &swapchain);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("IDXGIFactory::CreateSwapChain", result));
                    return nullptr;
                }

                auto rhiSwapchain = std::make_unique<D3D11Swapchain>(
                    device_, swapchain, RhiExtent2D{desc.width, desc.height}, desc.colorFormat, swapchainDesc.BufferCount, &lastError_);

                if (!rhiSwapchain->Initialize())
                {
                    return nullptr;
                }

                return rhiSwapchain;
            }

            [[nodiscard]] std::unique_ptr<RhiBuffer> CreateBuffer(const RhiBufferDesc& desc) override
            {
                D3D11_BUFFER_DESC bufferDesc = {};
                bufferDesc.ByteWidth = static_cast<UINT>(desc.size);
                bufferDesc.Usage = desc.memoryUsage == RhiBufferMemoryUsage::CpuToGpu ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
                bufferDesc.BindFlags = ToD3D11BufferBindFlags(desc.usage);
                bufferDesc.CPUAccessFlags = desc.memoryUsage == RhiBufferMemoryUsage::CpuToGpu ? D3D11_CPU_ACCESS_WRITE : 0;

                D3D11_SUBRESOURCE_DATA initialData = {};
                initialData.pSysMem = desc.initialData;

                ComPtr<ID3D11Buffer> buffer;
                HRESULT result = device_->CreateBuffer(&bufferDesc, desc.initialData != nullptr ? &initialData : nullptr, &buffer);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateBuffer", result));
                    return nullptr;
                }

                return std::make_unique<D3D11Buffer>(buffer, desc.size, desc.memoryUsage);
            }

            void UpdateBuffer(RhiBuffer& buffer, uint64_t offset, const void* data, uint64_t size, RhiBufferUpdateMode updateMode) override
            {
                auto& d3dBuffer = static_cast<D3D11Buffer&>(buffer);
                VE_ASSERT(d3dBuffer.GetMemoryUsage() == RhiBufferMemoryUsage::CpuToGpu);
                VE_ASSERT(data != nullptr);
                VE_ASSERT(size > 0);
                VE_ASSERT(offset + size <= d3dBuffer.GetSize());

                D3D11_MAPPED_SUBRESOURCE mappedResource = {};
                const D3D11_MAP mapType = updateMode == RhiBufferUpdateMode::Discard ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
                const HRESULT result = context_->Map(d3dBuffer.GetNativeBuffer(), 0, mapType, 0, &mappedResource);
                VE_ASSERT_MESSAGE(SUCCEEDED(result), "ID3D11DeviceContext::Map dynamic buffer failed.");
                if (FAILED(result))
                {
                    return;
                }

                std::memcpy(static_cast<std::byte*>(mappedResource.pData) + offset, data, static_cast<size_t>(size));
                context_->Unmap(d3dBuffer.GetNativeBuffer(), 0);
            }

            [[nodiscard]] std::unique_ptr<RhiTexture> CreateTexture(const RhiTextureDesc& desc) override
            {
                if (desc.dimension != RhiTextureDimension::Texture2D || desc.width == 0 || desc.height == 0)
                {
                    SetLastError("D3D11 texture requires a non-empty 2D descriptor.");
                    return nullptr;
                }
                if (!IsD3D11TextureDescSupported(desc))
                {
                    SetLastError("D3D11 texture descriptor has an unsupported depth sampling combination.");
                    return nullptr;
                }

                D3D11_TEXTURE2D_DESC textureDesc = {};
                textureDesc.Width = desc.width;
                textureDesc.Height = desc.height;
                textureDesc.MipLevels = desc.mipLevelCount;
                textureDesc.ArraySize = 1;
                textureDesc.Format = ToD3D11TextureResourceFormat(desc);
                textureDesc.SampleDesc.Count = 1;
                textureDesc.Usage = D3D11_USAGE_DEFAULT;
                textureDesc.BindFlags = ToD3D11TextureBindFlags(desc.usage);

                D3D11_SUBRESOURCE_DATA initialData = {};
                initialData.pSysMem = desc.initialData;
                initialData.SysMemPitch = desc.initialDataRowPitch;

                ComPtr<ID3D11Texture2D> texture;
                HRESULT result = device_->CreateTexture2D(&textureDesc, desc.initialData != nullptr ? &initialData : nullptr, &texture);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateTexture2D", result));
                    return nullptr;
                }

                const auto usageValue = static_cast<uint32_t>(desc.usage);
                ComPtr<ID3D11RenderTargetView> renderTargetView;
                if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::RenderTarget)) != 0)
                {
                    result = device_->CreateRenderTargetView(texture.Get(), nullptr, &renderTargetView);
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D11Device::CreateRenderTargetView texture", result));
                        return nullptr;
                    }
                }

                ComPtr<ID3D11DepthStencilView> depthStencilView;
                if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::DepthStencil)) != 0)
                {
                    D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc = {};
                    depthStencilViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
                    depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
                    result = device_->CreateDepthStencilView(texture.Get(), &depthStencilViewDesc, &depthStencilView);
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D11Device::CreateDepthStencilView texture", result));
                        return nullptr;
                    }
                }

                ComPtr<ID3D11ShaderResourceView> shaderResourceView;
                if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::Sampled)) != 0)
                {
                    D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc = {};
                    shaderResourceViewDesc.Format = desc.format == RhiFormat::Depth32Float ? DXGI_FORMAT_R32_FLOAT : ToDxgiFormat(desc.format);
                    shaderResourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    shaderResourceViewDesc.Texture2D.MipLevels = desc.mipLevelCount;
                    result = device_->CreateShaderResourceView(texture.Get(), &shaderResourceViewDesc, &shaderResourceView);
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D11Device::CreateShaderResourceView texture", result));
                        return nullptr;
                    }
                }

                return std::make_unique<D3D11Texture>(texture, std::move(renderTargetView), std::move(depthStencilView), std::move(shaderResourceView), desc);
            }

            [[nodiscard]] std::unique_ptr<RhiSampler> CreateSampler(const RhiSamplerDesc& desc) override
            {
                D3D11_SAMPLER_DESC samplerDesc = {};
                samplerDesc.Filter = ToD3D11Filter(desc.filter, desc.reductionMode);
                samplerDesc.AddressU = ToD3D11AddressMode(desc.addressU);
                samplerDesc.AddressV = ToD3D11AddressMode(desc.addressV);
                samplerDesc.AddressW = ToD3D11AddressMode(desc.addressW);
                samplerDesc.MipLODBias = desc.mipBias;
                samplerDesc.MaxAnisotropy = desc.maxAnisotropy;
                samplerDesc.ComparisonFunc = ToD3D11ComparisonFunc(desc.comparisonFunction);
                samplerDesc.BorderColor[0] = desc.borderColor.r;
                samplerDesc.BorderColor[1] = desc.borderColor.g;
                samplerDesc.BorderColor[2] = desc.borderColor.b;
                samplerDesc.BorderColor[3] = desc.borderColor.a;
                samplerDesc.MinLOD = desc.minLod;
                samplerDesc.MaxLOD = desc.maxLod;

                ComPtr<ID3D11SamplerState> samplerState;
                HRESULT result = device_->CreateSamplerState(&samplerDesc, &samplerState);
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateSamplerState", result));
                    return nullptr;
                }

                return std::make_unique<D3D11Sampler>(samplerState, desc);
            }

            [[nodiscard]] std::unique_ptr<RhiShaderModule> CreateShaderModule(const RhiShaderModuleDesc& desc) override
            {
                if (desc.codeFormat == RhiShaderCodeFormat::Bytecode)
                {
                    if (desc.bytecode == nullptr || desc.bytecodeSize == 0)
                    {
                        SetLastError("D3D11 shader module bytecode input is empty.");
                        return nullptr;
                    }

                    ComPtr<ID3DBlob> bytecode;
                    HRESULT result = D3DCreateBlob(static_cast<SIZE_T>(desc.bytecodeSize), &bytecode);
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("D3DCreateBlob", result));
                        return nullptr;
                    }

                    std::memcpy(bytecode->GetBufferPointer(), desc.bytecode, static_cast<size_t>(desc.bytecodeSize));
                    return std::make_unique<D3D11ShaderModule>(desc.stage, bytecode);
                }

                if (desc.source == nullptr || desc.entryPoint == nullptr)
                {
                    SetLastError("D3D11 shader module requires source and entry point.");
                    return nullptr;
                }

                const char* target = desc.stage == RhiShaderStage::Vertex ? "vs_5_0" : "ps_5_0";
                UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;

#if defined(_DEBUG)
                flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

                ComPtr<ID3DBlob> bytecode;
                ComPtr<ID3DBlob> errors;
                HRESULT result =
                    D3DCompile(desc.source, std::strlen(desc.source), desc.debugName, nullptr, nullptr, desc.entryPoint, target, flags, 0, &bytecode, &errors);

                if (FAILED(result))
                {
                    if (errors != nullptr)
                    {
                        lastError_.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
                    }
                    else
                    {
                        SetLastError(MakeHResultError("D3DCompile", result));
                    }

                    return nullptr;
                }

                return std::make_unique<D3D11ShaderModule>(desc.stage, bytecode);
            }

            [[nodiscard]] std::unique_ptr<RhiPipelineState> CreateGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) override
            {
                if (!IsPipelineResourceLayoutValid(desc.resourceLayout))
                {
                    SetLastError("D3D11 graphics pipeline resource layout is invalid or contains duplicate bindings.");
                    return nullptr;
                }
                if (desc.colorAttachmentCount > 1 || (desc.colorAttachmentCount == 0 && desc.colorFormat != RhiFormat::Unknown) ||
                    (desc.colorAttachmentCount == 1 && (desc.colorFormat == RhiFormat::Unknown || desc.colorFormat == RhiFormat::Depth32Float)))
                {
                    SetLastError("D3D11 graphics pipeline requires zero or one valid color attachment format.");
                    return nullptr;
                }

                const RhiBoundShaderStateDesc& boundShaderState = desc.boundShaderState;
                const auto* vertexShaderModule = dynamic_cast<const D3D11ShaderModule*>(boundShaderState.vertexShader);
                const auto* fragmentShaderModule =
                    boundShaderState.fragmentShader != nullptr ? dynamic_cast<const D3D11ShaderModule*>(boundShaderState.fragmentShader) : nullptr;

                if (vertexShaderModule == nullptr || vertexShaderModule->GetStage() != RhiShaderStage::Vertex ||
                    (boundShaderState.fragmentShader != nullptr &&
                     (fragmentShaderModule == nullptr || fragmentShaderModule->GetStage() != RhiShaderStage::Fragment)) ||
                    (desc.colorAttachmentCount == 1 && fragmentShaderModule == nullptr))
                {
                    SetLastError("D3D11 graphics pipeline requires a D3D11 vertex shader and a fragment shader for color output.");
                    return nullptr;
                }

                ComPtr<ID3D11VertexShader> vertexShader;
                HRESULT result = device_->CreateVertexShader(
                    vertexShaderModule->GetBytecode()->GetBufferPointer(), vertexShaderModule->GetBytecode()->GetBufferSize(), nullptr, &vertexShader);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateVertexShader", result));
                    return nullptr;
                }

                ComPtr<ID3D11PixelShader> pixelShader;
                if (fragmentShaderModule != nullptr)
                {
                    result = device_->CreatePixelShader(
                        fragmentShaderModule->GetBytecode()->GetBufferPointer(), fragmentShaderModule->GetBytecode()->GetBufferSize(), nullptr, &pixelShader);

                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D11Device::CreatePixelShader", result));
                        return nullptr;
                    }
                }

                std::vector<D3D11_INPUT_ELEMENT_DESC> inputElements;
                inputElements.reserve(boundShaderState.vertexDeclaration.attributeCount);

                for (uint32_t index = 0; index < boundShaderState.vertexDeclaration.attributeCount; ++index)
                {
                    const RhiVertexAttributeDesc& attribute = boundShaderState.vertexDeclaration.attributes[index];
                    D3D11_INPUT_ELEMENT_DESC inputElement = {};
                    inputElement.SemanticName = attribute.semanticName;
                    inputElement.SemanticIndex = attribute.semanticIndex;
                    inputElement.Format = ToDxgiFormat(attribute.format);
                    inputElement.InputSlot = 0;
                    inputElement.AlignedByteOffset = attribute.offset;
                    inputElement.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
                    inputElement.InstanceDataStepRate = 0;
                    inputElements.push_back(inputElement);
                }

                ComPtr<ID3D11InputLayout> inputLayout;
                if (!inputElements.empty())
                {
                    result = device_->CreateInputLayout(inputElements.data(),
                                                        static_cast<UINT>(inputElements.size()),
                                                        vertexShaderModule->GetBytecode()->GetBufferPointer(),
                                                        vertexShaderModule->GetBytecode()->GetBufferSize(),
                                                        &inputLayout);

                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D11Device::CreateInputLayout", result));
                        return nullptr;
                    }
                }

                D3D11_RASTERIZER_DESC rasterizerDesc = {};
                rasterizerDesc.FillMode = desc.rasterizerState.fillMode == RhiFillMode::Wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
                rasterizerDesc.CullMode = ToD3D11CullMode(desc.rasterizerState.cullMode);
                rasterizerDesc.FrontCounterClockwise = desc.rasterizerState.frontCounterClockwise ? TRUE : FALSE;
                rasterizerDesc.DepthBias = desc.rasterizerState.depthBias;
                rasterizerDesc.DepthBiasClamp = desc.rasterizerState.depthBiasClamp;
                rasterizerDesc.SlopeScaledDepthBias = desc.rasterizerState.slopeScaledDepthBias;
                rasterizerDesc.DepthClipEnable = desc.rasterizerState.depthClipEnabled ? TRUE : FALSE;
                rasterizerDesc.ScissorEnable = desc.rasterizerState.scissorEnabled ? TRUE : FALSE;
                rasterizerDesc.MultisampleEnable = desc.rasterizerState.multisampleEnabled ? TRUE : FALSE;
                rasterizerDesc.AntialiasedLineEnable = desc.rasterizerState.antialiasedLineEnabled ? TRUE : FALSE;

                ComPtr<ID3D11RasterizerState> rasterizerState;
                result = device_->CreateRasterizerState(&rasterizerDesc, &rasterizerState);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateRasterizerState", result));
                    return nullptr;
                }

                D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
                depthStencilDesc.DepthEnable = desc.depthStencilState.depthTestEnabled ? TRUE : FALSE;
                depthStencilDesc.DepthWriteMask = desc.depthStencilState.depthWriteEnabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
                depthStencilDesc.DepthFunc = ToD3D11ComparisonFunc(desc.depthStencilState.depthCompareFunction);
                depthStencilDesc.StencilEnable = desc.depthStencilState.stencilEnabled ? TRUE : FALSE;
                depthStencilDesc.StencilReadMask = desc.depthStencilState.stencilReadMask;
                depthStencilDesc.StencilWriteMask = desc.depthStencilState.stencilWriteMask;
                depthStencilDesc.FrontFace = ToD3D11StencilFaceDesc(desc.depthStencilState.frontFace);
                depthStencilDesc.BackFace = ToD3D11StencilFaceDesc(desc.depthStencilState.backFace);

                ComPtr<ID3D11DepthStencilState> depthStencilState;
                result = device_->CreateDepthStencilState(&depthStencilDesc, &depthStencilState);
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateDepthStencilState", result));
                    return nullptr;
                }

                D3D11_BLEND_DESC blendDesc = {};
                blendDesc.AlphaToCoverageEnable = desc.blendState.alphaToCoverageEnabled ? TRUE : FALSE;
                blendDesc.IndependentBlendEnable = desc.blendState.independentBlendEnabled ? TRUE : FALSE;
                for (uint32_t index = 0; index < RhiMaxColorAttachments; ++index)
                {
                    const RhiBlendRenderTargetDesc& targetDesc = desc.blendState.renderTargets[index];
                    blendDesc.RenderTarget[index].BlendEnable = targetDesc.blendEnabled ? TRUE : FALSE;
                    blendDesc.RenderTarget[index].SrcBlend = ToD3D11Blend(targetDesc.sourceColorBlendFactor);
                    blendDesc.RenderTarget[index].DestBlend = ToD3D11Blend(targetDesc.destinationColorBlendFactor);
                    blendDesc.RenderTarget[index].BlendOp = ToD3D11BlendOp(targetDesc.colorBlendOperation);
                    blendDesc.RenderTarget[index].SrcBlendAlpha = ToD3D11Blend(targetDesc.sourceAlphaBlendFactor);
                    blendDesc.RenderTarget[index].DestBlendAlpha = ToD3D11Blend(targetDesc.destinationAlphaBlendFactor);
                    blendDesc.RenderTarget[index].BlendOpAlpha = ToD3D11BlendOp(targetDesc.alphaBlendOperation);
                    blendDesc.RenderTarget[index].RenderTargetWriteMask = ToD3D11ColorWriteMask(targetDesc.colorWriteMask);
                }

                ComPtr<ID3D11BlendState> blendState;
                result = device_->CreateBlendState(&blendDesc, &blendState);
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateBlendState", result));
                    return nullptr;
                }

                std::vector<RhiPipelineResourceBindingDesc> resourceBindings;
                if (desc.resourceLayout.bindingCount != 0)
                {
                    resourceBindings.assign(desc.resourceLayout.bindings, desc.resourceLayout.bindings + desc.resourceLayout.bindingCount);
                }
                return std::make_unique<D3D11PipelineState>(
                    desc.primitiveType, vertexShader, pixelShader, inputLayout, rasterizerState, depthStencilState, blendState, std::move(resourceBindings));
            }

            [[nodiscard]] std::unique_ptr<RhiCommandList> CreateCommandList() override
            {
                return std::make_unique<D3D11CommandList>(context_, context1_);
            }

            [[nodiscard]] std::unique_ptr<RhiFence> CreateFence(uint64_t initialValue = 0) override
            {
                return std::make_unique<D3D11Fence>(device_, context_, initialValue);
            }

            [[nodiscard]] bool Submit(RhiCommandList& commandList, RhiFence* completionFence, uint64_t completionValue) override
            {
                auto* d3dCommandList = dynamic_cast<D3D11CommandList*>(&commandList);
                if (d3dCommandList == nullptr)
                {
                    SetLastError("D3D11 device can only submit D3D11 command lists.");
                    return false;
                }

                if (completionFence == nullptr)
                {
                    return true;
                }

                auto* d3dFence = dynamic_cast<D3D11Fence*>(completionFence);
                if (d3dFence == nullptr)
                {
                    SetLastError("D3D11 device can only signal D3D11 fences.");
                    return false;
                }

                if (!d3dFence->Signal(completionValue))
                {
                    SetLastError("D3D11 fence signal failed.");
                    return false;
                }

                return true;
            }

            void WaitIdle() override
            {
                D3D11Fence fence(device_, context_, 0);
                if (fence.Signal(1))
                {
                    context_->Flush();
                    (void)fence.Wait(1);
                }
            }

        private:
            void SetLastError(std::string error)
            {
                lastError_ = std::move(error);
            }

        private:
            bool enableDebug_ = false;
            ComPtr<ID3D11Device> device_;
            ComPtr<ID3D11DeviceContext> context_;
            ComPtr<ID3D11DeviceContext1> context1_;
            std::string lastError_;
        };
    } // namespace

    std::unique_ptr<RhiDevice> CreateD3D11Device(bool enableDebug)
    {
        auto device = std::make_unique<D3D11Device>(enableDebug);

        if (!device->Initialize())
        {
            return nullptr;
        }

        return device;
    }
} // namespace ve::rhi
