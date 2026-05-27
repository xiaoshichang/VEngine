#include "Engine/RHI/D3D11/D3D11Rhi.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <d3d11.h>
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
            case RhiFormat::Rg32Float:
                return DXGI_FORMAT_R32G32_FLOAT;
            case RhiFormat::Rgb32Float:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case RhiFormat::Unknown:
            default:
                return DXGI_FORMAT_UNKNOWN;
            }
        }

        D3D11_PRIMITIVE_TOPOLOGY ToD3D11Topology(RhiPrimitiveTopology topology)
        {
            switch (topology)
            {
            case RhiPrimitiveTopology::TriangleList:
            default:
                return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            }
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

            return flags;
        }

        UINT ToD3D11BufferBindFlags(RhiBufferUsage usage)
        {
            UINT flags = 0;
            const auto usageValue = static_cast<uint32_t>(usage);

            if ((usageValue & static_cast<uint32_t>(RhiBufferUsage::Vertex)) != 0)
            {
                flags |= D3D11_BIND_VERTEX_BUFFER;
            }

            if ((usageValue & static_cast<uint32_t>(RhiBufferUsage::Uniform)) != 0)
            {
                flags |= D3D11_BIND_CONSTANT_BUFFER;
            }

            if ((usageValue & static_cast<uint32_t>(RhiBufferUsage::Index)) != 0)
            {
                flags |= D3D11_BIND_INDEX_BUFFER;
            }

            return flags;
        }

        DXGI_FORMAT ToD3D11IndexFormat(RhiIndexFormat format)
        {
            switch (format)
            {
            case RhiIndexFormat::UInt16:
                return DXGI_FORMAT_R16_UINT;
            case RhiIndexFormat::UInt32:
            default:
                return DXGI_FORMAT_R32_UINT;
            }
        }

        D3D11_CULL_MODE ToD3D11CullMode(RhiCullMode cullMode)
        {
            switch (cullMode)
            {
            case RhiCullMode::None:
                return D3D11_CULL_NONE;
            case RhiCullMode::Back:
            default:
                return D3D11_CULL_BACK;
            }
        }

        std::string MakeHResultError(const char* operation, HRESULT result)
        {
            char buffer[128] = {};
            std::snprintf(
                buffer, sizeof(buffer), "%s failed with HRESULT 0x%08X", operation, static_cast<unsigned>(result));
            return buffer;
        }

        class D3D11Buffer final : public RhiBuffer
        {
        public:
            D3D11Buffer(ComPtr<ID3D11Buffer> buffer, uint64_t size)
                : buffer_(std::move(buffer))
                , size_(size)
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

        private:
            ComPtr<ID3D11Buffer> buffer_;
            uint64_t size_ = 0;
        };

        class D3D11Texture final : public RhiTexture
        {
        public:
            D3D11Texture(ComPtr<ID3D11Texture2D> texture,
                         ComPtr<ID3D11ShaderResourceView> shaderResourceView,
                         RhiTextureDesc desc)
                : texture_(std::move(texture))
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

            [[nodiscard]] ID3D11ShaderResourceView* GetShaderResourceView() const noexcept
            {
                return shaderResourceView_.Get();
            }

        private:
            ComPtr<ID3D11Texture2D> texture_;
            ComPtr<ID3D11ShaderResourceView> shaderResourceView_;
            RhiTextureDesc desc_ = {};
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
                               ComPtr<ID3D11BlendState> blendState)
                : topology_(topology)
                , vertexShader_(std::move(vertexShader))
                , pixelShader_(std::move(pixelShader))
                , inputLayout_(std::move(inputLayout))
                , rasterizerState_(std::move(rasterizerState))
                , blendState_(std::move(blendState))
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
                context->VSSetShader(vertexShader_.Get(), nullptr, 0);
                context->PSSetShader(pixelShader_.Get(), nullptr, 0);
                context->RSSetState(rasterizerState_.Get());

                const float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                context->OMSetBlendState(blendState_.Get(), blendFactor, 0xFFFFFFFFu);
            }

        private:
            RhiPrimitiveTopology topology_ = RhiPrimitiveTopology::TriangleList;
            ComPtr<ID3D11VertexShader> vertexShader_;
            ComPtr<ID3D11PixelShader> pixelShader_;
            ComPtr<ID3D11InputLayout> inputLayout_;
            ComPtr<ID3D11RasterizerState> rasterizerState_;
            ComPtr<ID3D11BlendState> blendState_;
        };

        class D3D11Swapchain final : public RhiSwapchain
        {
        public:
            D3D11Swapchain(ComPtr<ID3D11Device> device,
                           ComPtr<IDXGISwapChain> swapchain,
                           RhiExtent2D extent,
                           RhiFormat colorFormat,
                           std::string* lastError)
                : device_(std::move(device))
                , swapchain_(std::move(swapchain))
                , extent_(extent)
                , colorFormat_(colorFormat)
                , lastError_(lastError)
            {
            }

            [[nodiscard]] bool Initialize()
            {
                ComPtr<ID3D11Texture2D> backBuffer;
                HRESULT result = swapchain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("IDXGISwapChain::GetBuffer", result));
                    return false;
                }

                result = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView_);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateRenderTargetView", result));
                    return false;
                }

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

        private:
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
            ComPtr<ID3D11RenderTargetView> renderTargetView_;
            RhiExtent2D extent_ = {};
            RhiFormat colorFormat_ = RhiFormat::Bgra8Unorm;
            std::string* lastError_ = nullptr;
        };

        class D3D11CommandList final : public RhiCommandList
        {
        public:
            D3D11CommandList(ComPtr<ID3D11DeviceContext> context, ComPtr<ID3D11SamplerState> defaultSampler)
                : context_(std::move(context))
                , defaultSampler_(std::move(defaultSampler))
            {
            }

            [[nodiscard]] bool Begin() override
            {
                return true;
            }

            [[nodiscard]] bool End() override
            {
                return true;
            }

            [[nodiscard]] bool BeginRenderPass(RhiSwapchain& swapchain, const RhiRenderPassDesc& desc) override
            {
                auto* d3dSwapchain = dynamic_cast<D3D11Swapchain*>(&swapchain);

                if (d3dSwapchain == nullptr)
                {
                    return false;
                }

                ID3D11RenderTargetView* renderTargetView = d3dSwapchain->GetRenderTargetView();
                context_->OMSetRenderTargets(1, &renderTargetView, nullptr);

                if (desc.colorLoadAction == RhiLoadAction::Clear)
                {
                    const float clearColor[4] = {
                        desc.clearColor.r, desc.clearColor.g, desc.clearColor.b, desc.clearColor.a};
                    context_->ClearRenderTargetView(renderTargetView, clearColor);
                }

                return true;
            }

            void EndRenderPass() override
            {
                ID3D11RenderTargetView* nullRenderTarget = nullptr;
                context_->OMSetRenderTargets(1, &nullRenderTarget, nullptr);
            }

            void SetPipeline(const RhiPipelineState& pipelineState) override
            {
                const auto& d3dPipelineState = static_cast<const D3D11PipelineState&>(pipelineState);
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
                context_->IASetIndexBuffer(
                    d3dBuffer.GetNativeBuffer(), ToD3D11IndexFormat(format), static_cast<UINT>(offset));
            }

            void SetUniformBuffer(RhiShaderStage stage,
                                  uint32_t slot,
                                  const RhiBuffer& buffer,
                                  uint64_t offset,
                                  uint64_t size) override
            {
                (void)offset;
                (void)size;

                const auto& d3dBuffer = static_cast<const D3D11Buffer&>(buffer);
                ID3D11Buffer* nativeBuffer = d3dBuffer.GetNativeBuffer();

                switch (stage)
                {
                case RhiShaderStage::Vertex:
                    context_->VSSetConstantBuffers(slot, 1, &nativeBuffer);
                    break;
                case RhiShaderStage::Fragment:
                    context_->PSSetConstantBuffers(slot, 1, &nativeBuffer);
                    break;
                }
            }

            void SetTexture(RhiShaderStage stage, uint32_t slot, const RhiTexture& texture) override
            {
                const auto& d3dTexture = static_cast<const D3D11Texture&>(texture);
                ID3D11ShaderResourceView* shaderResourceView = d3dTexture.GetShaderResourceView();
                ID3D11SamplerState* sampler = defaultSampler_.Get();

                switch (stage)
                {
                case RhiShaderStage::Vertex:
                    context_->VSSetShaderResources(slot, 1, &shaderResourceView);
                    context_->VSSetSamplers(slot, 1, &sampler);
                    break;
                case RhiShaderStage::Fragment:
                    context_->PSSetShaderResources(slot, 1, &shaderResourceView);
                    context_->PSSetSamplers(slot, 1, &sampler);
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
            ComPtr<ID3D11DeviceContext> context_;
            ComPtr<ID3D11SamplerState> defaultSampler_;
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

                D3D11_SAMPLER_DESC samplerDesc = {};
                samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
                samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
                samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
                samplerDesc.MinLOD = 0.0f;
                samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

                result = device_->CreateSamplerState(&samplerDesc, &defaultSampler_);
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateSamplerState", result));
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
                    device_, swapchain, RhiExtent2D{desc.width, desc.height}, desc.colorFormat, &lastError_);

                if (!rhiSwapchain->Initialize())
                {
                    return nullptr;
                }

                return rhiSwapchain;
            }

            [[nodiscard]] std::unique_ptr<RhiBuffer> CreateBuffer(const RhiBufferDesc& desc) override
            {
                if (desc.size == 0)
                {
                    SetLastError("D3D11 buffer requires a non-zero size.");
                    return nullptr;
                }

                const auto usageValue = static_cast<uint32_t>(desc.usage);
                if ((usageValue & static_cast<uint32_t>(RhiBufferUsage::Uniform)) != 0 && (desc.size % 16) != 0)
                {
                    SetLastError("D3D11 constant buffer size must be aligned to 16 bytes.");
                    return nullptr;
                }

                D3D11_BUFFER_DESC bufferDesc = {};
                bufferDesc.ByteWidth = static_cast<UINT>(desc.size);
                bufferDesc.Usage = D3D11_USAGE_DEFAULT;
                bufferDesc.BindFlags = ToD3D11BufferBindFlags(desc.usage);

                D3D11_SUBRESOURCE_DATA initialData = {};
                initialData.pSysMem = desc.initialData;

                ComPtr<ID3D11Buffer> buffer;
                HRESULT result =
                    device_->CreateBuffer(&bufferDesc, desc.initialData != nullptr ? &initialData : nullptr, &buffer);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateBuffer", result));
                    return nullptr;
                }

                return std::make_unique<D3D11Buffer>(buffer, desc.size);
            }

            [[nodiscard]] std::unique_ptr<RhiTexture> CreateTexture(const RhiTextureDesc& desc) override
            {
                if (desc.dimension != RhiTextureDimension::Texture2D || desc.width == 0 || desc.height == 0)
                {
                    SetLastError("D3D11 texture requires a non-empty 2D descriptor.");
                    return nullptr;
                }

                D3D11_TEXTURE2D_DESC textureDesc = {};
                textureDesc.Width = desc.width;
                textureDesc.Height = desc.height;
                textureDesc.MipLevels = desc.mipLevelCount;
                textureDesc.ArraySize = 1;
                textureDesc.Format = ToDxgiFormat(desc.format);
                textureDesc.SampleDesc.Count = 1;
                textureDesc.Usage = D3D11_USAGE_DEFAULT;
                textureDesc.BindFlags = ToD3D11TextureBindFlags(desc.usage);

                D3D11_SUBRESOURCE_DATA initialData = {};
                initialData.pSysMem = desc.initialData;
                initialData.SysMemPitch = desc.initialDataRowPitch;

                ComPtr<ID3D11Texture2D> texture;
                HRESULT result = device_->CreateTexture2D(
                    &textureDesc, desc.initialData != nullptr ? &initialData : nullptr, &texture);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateTexture2D", result));
                    return nullptr;
                }

                ComPtr<ID3D11ShaderResourceView> shaderResourceView;
                const auto usageValue = static_cast<uint32_t>(desc.usage);
                if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::Sampled)) != 0)
                {
                    result = device_->CreateShaderResourceView(texture.Get(), nullptr, &shaderResourceView);
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D11Device::CreateShaderResourceView", result));
                        return nullptr;
                    }
                }

                return std::make_unique<D3D11Texture>(texture, shaderResourceView, desc);
            }

            [[nodiscard]] std::unique_ptr<RhiShaderModule> CreateShaderModule(const RhiShaderModuleDesc& desc) override
            {
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
                HRESULT result = D3DCompile(desc.source,
                                            std::strlen(desc.source),
                                            desc.debugName,
                                            nullptr,
                                            nullptr,
                                            desc.entryPoint,
                                            target,
                                            flags,
                                            0,
                                            &bytecode,
                                            &errors);

                if (FAILED(result))
                {
                    if (errors != nullptr)
                    {
                        lastError_.assign(static_cast<const char*>(errors->GetBufferPointer()),
                                          errors->GetBufferSize());
                    }
                    else
                    {
                        SetLastError(MakeHResultError("D3DCompile", result));
                    }

                    return nullptr;
                }

                return std::make_unique<D3D11ShaderModule>(desc.stage, bytecode);
            }

            [[nodiscard]] std::unique_ptr<RhiPipelineState>
            CreateGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) override
            {
                const auto* vertexShaderModule = dynamic_cast<const D3D11ShaderModule*>(desc.vertexShader);
                const auto* fragmentShaderModule = dynamic_cast<const D3D11ShaderModule*>(desc.fragmentShader);

                if (vertexShaderModule == nullptr || fragmentShaderModule == nullptr)
                {
                    SetLastError("D3D11 graphics pipeline requires D3D11 shader modules.");
                    return nullptr;
                }

                ComPtr<ID3D11VertexShader> vertexShader;
                HRESULT result = device_->CreateVertexShader(vertexShaderModule->GetBytecode()->GetBufferPointer(),
                                                             vertexShaderModule->GetBytecode()->GetBufferSize(),
                                                             nullptr,
                                                             &vertexShader);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateVertexShader", result));
                    return nullptr;
                }

                ComPtr<ID3D11PixelShader> pixelShader;
                result = device_->CreatePixelShader(fragmentShaderModule->GetBytecode()->GetBufferPointer(),
                                                    fragmentShaderModule->GetBytecode()->GetBufferSize(),
                                                    nullptr,
                                                    &pixelShader);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreatePixelShader", result));
                    return nullptr;
                }

                std::vector<D3D11_INPUT_ELEMENT_DESC> inputElements;
                inputElements.reserve(desc.vertexLayout.attributeCount);

                for (uint32_t index = 0; index < desc.vertexLayout.attributeCount; ++index)
                {
                    const RhiVertexAttributeDesc& attribute = desc.vertexLayout.attributes[index];
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

                D3D11_RASTERIZER_DESC rasterizerDesc = {};
                rasterizerDesc.FillMode = D3D11_FILL_SOLID;
                rasterizerDesc.CullMode = ToD3D11CullMode(desc.cullMode);
                rasterizerDesc.FrontCounterClockwise = FALSE;
                rasterizerDesc.DepthClipEnable = TRUE;
                rasterizerDesc.ScissorEnable = desc.enableScissor ? TRUE : FALSE;

                ComPtr<ID3D11RasterizerState> rasterizerState;
                result = device_->CreateRasterizerState(&rasterizerDesc, &rasterizerState);
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateRasterizerState", result));
                    return nullptr;
                }

                D3D11_BLEND_DESC blendDesc = {};
                blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
                if (desc.enableAlphaBlending)
                {
                    blendDesc.RenderTarget[0].BlendEnable = TRUE;
                    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
                    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
                    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
                    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
                    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
                }

                ComPtr<ID3D11BlendState> blendState;
                result = device_->CreateBlendState(&blendDesc, &blendState);
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateBlendState", result));
                    return nullptr;
                }

                return std::make_unique<D3D11PipelineState>(
                    desc.topology, vertexShader, pixelShader, inputLayout, rasterizerState, blendState);
            }

            [[nodiscard]] std::unique_ptr<RhiCommandList> CreateCommandList() override
            {
                return std::make_unique<D3D11CommandList>(context_, defaultSampler_);
            }

            [[nodiscard]] std::unique_ptr<RhiFence> CreateFence(uint64_t initialValue = 0) override
            {
                D3D11_QUERY_DESC queryDesc = {};
                queryDesc.Query = D3D11_QUERY_EVENT;

                ComPtr<ID3D11Query> query;
                HRESULT result = device_->CreateQuery(&queryDesc, &query);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D11Device::CreateQuery fence", result));
                    return nullptr;
                }

                context_->End(query.Get());
                return std::make_unique<D3D11Fence>(device_, context_, initialValue);
            }

            [[nodiscard]] bool SignalFence(RhiFence& fence, uint64_t value) override
            {
                auto* d3dFence = dynamic_cast<D3D11Fence*>(&fence);
                if (d3dFence == nullptr)
                {
                    SetLastError("D3D11 device can only signal D3D11 fences.");
                    return false;
                }

                if (!d3dFence->Signal(value))
                {
                    SetLastError("D3D11 fence signal failed.");
                    return false;
                }

                return true;
            }

            [[nodiscard]] bool Submit(RhiCommandList& commandList) override
            {
                (void)commandList;
                return true;
            }

            void WaitIdle() override
            {
                context_->Flush();
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
            ComPtr<ID3D11SamplerState> defaultSampler_;
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
