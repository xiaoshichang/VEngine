#include "Engine/RHI/D3D12/D3D12Rhi.h"

#include "Engine/Runtime/Core/Assert.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <memory>
#include <string>
#include <utility>
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

        D3D12_PRIMITIVE_TOPOLOGY_TYPE ToD3D12TopologyType(RhiPrimitiveTopology topology)
        {
            switch (topology)
            {
            case RhiPrimitiveTopology::TriangleList:
            default:
                return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            }
        }

        D3D_PRIMITIVE_TOPOLOGY ToD3DTopology(RhiPrimitiveTopology topology)
        {
            switch (topology)
            {
            case RhiPrimitiveTopology::TriangleList:
            default:
                return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            }
        }

        D3D12_RESOURCE_FLAGS ToD3D12TextureFlags(RhiTextureUsage usage)
        {
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
            const auto usageValue = static_cast<uint32_t>(usage);

            if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::RenderTarget)) != 0)
            {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            }

            if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::DepthStencil)) != 0)
            {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            }

            return flags;
        }

        std::string MakeHResultError(const char* operation, HRESULT result)
        {
            char buffer[128] = {};
            std::snprintf(buffer, sizeof(buffer), "%s failed with HRESULT 0x%08X", operation, static_cast<unsigned>(result));
            return buffer;
        }

        class D3D12Buffer final : public RhiBuffer
        {
        public:
            D3D12Buffer(ComPtr<ID3D12Resource> resource, uint64_t size)
                : resource_(std::move(resource))
                , size_(size)
            {
            }

            [[nodiscard]] uint64_t GetSize() const noexcept override
            {
                return size_;
            }

            [[nodiscard]] ID3D12Resource* GetNativeResource() const noexcept
            {
                return resource_.Get();
            }

        private:
            ComPtr<ID3D12Resource> resource_;
            uint64_t size_ = 0;
        };

        class D3D12Texture final : public RhiTexture
        {
        public:
            D3D12Texture(ComPtr<ID3D12Resource> resource,
                         ComPtr<ID3D12DescriptorHeap> rtvHeap,
                         ComPtr<ID3D12DescriptorHeap> dsvHeap,
                         RhiTextureDesc desc)
                : resource_(std::move(resource))
                , rtvHeap_(std::move(rtvHeap))
                , dsvHeap_(std::move(dsvHeap))
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

            [[nodiscard]] ID3D12Resource* GetNativeResource() const noexcept
            {
                return resource_.Get();
            }

            [[nodiscard]] bool HasRenderTargetView() const noexcept
            {
                return rtvHeap_ != nullptr;
            }

            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView() const noexcept
            {
                VE_ASSERT(rtvHeap_ != nullptr);
                return rtvHeap_->GetCPUDescriptorHandleForHeapStart();
            }

            [[nodiscard]] bool HasDepthStencilView() const noexcept
            {
                return dsvHeap_ != nullptr;
            }

            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const noexcept
            {
                VE_ASSERT(dsvHeap_ != nullptr);
                return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
            }

            [[nodiscard]] D3D12_RESOURCE_STATES GetResourceState() const noexcept
            {
                return resourceState_;
            }

            void SetResourceState(D3D12_RESOURCE_STATES state) noexcept
            {
                resourceState_ = state;
            }

        private:
            ComPtr<ID3D12Resource> resource_;
            ComPtr<ID3D12DescriptorHeap> rtvHeap_;
            ComPtr<ID3D12DescriptorHeap> dsvHeap_;
            RhiTextureDesc desc_ = {};
            D3D12_RESOURCE_STATES resourceState_ = D3D12_RESOURCE_STATE_COMMON;
        };

        class D3D12FenceObject final : public RhiFence
        {
        public:
            D3D12FenceObject(ComPtr<ID3D12Fence> fence, HANDLE fenceEvent)
                : fence_(std::move(fence))
                , fenceEvent_(fenceEvent)
            {
            }

            [[nodiscard]] ID3D12Fence* GetNativeFence() const noexcept
            {
                return fence_.Get();
            }

            [[nodiscard]] bool IsComplete(uint64_t value) const noexcept override
            {
                return GetCompletedValue() >= value;
            }

            [[nodiscard]] bool Wait(uint64_t value) override
            {
                if (IsComplete(value))
                {
                    return true;
                }

                HRESULT result = fence_->SetEventOnCompletion(value, fenceEvent_);
                if (FAILED(result))
                {
                    return false;
                }

                WaitForSingleObject(fenceEvent_, INFINITE);
                return true;
            }

            [[nodiscard]] uint64_t GetCompletedValue() const noexcept override
            {
                return fence_->GetCompletedValue();
            }

        private:
            ComPtr<ID3D12Fence> fence_;
            HANDLE fenceEvent_ = nullptr;
        };

        class D3D12ShaderModule final : public RhiShaderModule
        {
        public:
            D3D12ShaderModule(RhiShaderStage stage, ComPtr<ID3DBlob> bytecode)
                : stage_(stage)
                , bytecode_(std::move(bytecode))
            {
            }

            [[nodiscard]] RhiShaderStage GetStage() const noexcept override
            {
                return stage_;
            }

            [[nodiscard]] D3D12_SHADER_BYTECODE GetBytecode() const noexcept
            {
                D3D12_SHADER_BYTECODE bytecode = {};
                bytecode.pShaderBytecode = bytecode_->GetBufferPointer();
                bytecode.BytecodeLength = bytecode_->GetBufferSize();
                return bytecode;
            }

        private:
            RhiShaderStage stage_ = RhiShaderStage::Vertex;
            ComPtr<ID3DBlob> bytecode_;
        };

        class D3D12PipelineState final : public RhiPipelineState
        {
        public:
            D3D12PipelineState(RhiPrimitiveTopology topology, ComPtr<ID3D12RootSignature> rootSignature, ComPtr<ID3D12PipelineState> pipelineState)
                : topology_(topology)
                , rootSignature_(std::move(rootSignature))
                , pipelineState_(std::move(pipelineState))
            {
            }

            [[nodiscard]] RhiPrimitiveTopology GetTopology() const noexcept override
            {
                return topology_;
            }

            [[nodiscard]] ID3D12RootSignature* GetRootSignature() const noexcept
            {
                return rootSignature_.Get();
            }

            [[nodiscard]] ID3D12PipelineState* GetPipelineState() const noexcept
            {
                return pipelineState_.Get();
            }

        private:
            RhiPrimitiveTopology topology_ = RhiPrimitiveTopology::TriangleList;
            ComPtr<ID3D12RootSignature> rootSignature_;
            ComPtr<ID3D12PipelineState> pipelineState_;
        };

        class D3D12Swapchain final : public RhiSwapchain
        {
        public:
            D3D12Swapchain(ComPtr<ID3D12Device> device,
                           ComPtr<IDXGISwapChain3> swapchain,
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
                D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
                heapDesc.NumDescriptors = bufferCount_;
                heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

                HRESULT result = device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap_));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateDescriptorHeap RTV", result));
                    return false;
                }

                rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                renderTargets_.resize(bufferCount_);

                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

                for (uint32_t index = 0; index < bufferCount_; ++index)
                {
                    result = swapchain_->GetBuffer(index, IID_PPV_ARGS(&renderTargets_[index]));

                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("IDXGISwapChain3::GetBuffer", result));
                        return false;
                    }

                    device_->CreateRenderTargetView(renderTargets_[index].Get(), nullptr, rtvHandle);
                    rtvHandle.ptr += rtvDescriptorSize_;
                }

                frameIndex_ = swapchain_->GetCurrentBackBufferIndex();
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
                    SetLastError(MakeHResultError("IDXGISwapChain3::Present", result));
                    return false;
                }

                frameIndex_ = swapchain_->GetCurrentBackBufferIndex();
                return true;
            }

            [[nodiscard]] ID3D12Resource* GetCurrentRenderTarget() const noexcept
            {
                return renderTargets_[frameIndex_].Get();
            }

            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRtv() const noexcept
            {
                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
                rtvHandle.ptr += static_cast<SIZE_T>(frameIndex_) * rtvDescriptorSize_;
                return rtvHandle;
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
            ComPtr<ID3D12Device> device_;
            ComPtr<IDXGISwapChain3> swapchain_;
            ComPtr<ID3D12DescriptorHeap> rtvHeap_;
            std::vector<ComPtr<ID3D12Resource>> renderTargets_;
            RhiExtent2D extent_ = {};
            RhiFormat colorFormat_ = RhiFormat::Bgra8Unorm;
            uint32_t bufferCount_ = 2;
            uint32_t frameIndex_ = 0;
            uint32_t rtvDescriptorSize_ = 0;
            std::string* lastError_ = nullptr;
        };

        class D3D12CommandList final : public RhiCommandList
        {
        public:
            D3D12CommandList(ComPtr<ID3D12Device> device, ComPtr<ID3D12CommandAllocator> commandAllocator, ComPtr<ID3D12GraphicsCommandList> commandList)
                : device_(std::move(device))
                , commandAllocator_(std::move(commandAllocator))
                , commandList_(std::move(commandList))
            {
            }

            [[nodiscard]] bool Begin() override
            {
                HRESULT result = commandAllocator_->Reset();

                if (FAILED(result))
                {
                    return false;
                }

                result = commandList_->Reset(commandAllocator_.Get(), nullptr);
                return SUCCEEDED(result);
            }

            [[nodiscard]] bool End() override
            {
                return SUCCEEDED(commandList_->Close());
            }

            [[nodiscard]] bool BeginRenderPass(RhiSwapchain& swapchain, const RhiRenderPassDesc& desc) override
            {
                auto* d3dSwapchain = dynamic_cast<D3D12Swapchain*>(&swapchain);

                if (d3dSwapchain == nullptr || desc.colorAttachmentCount == 0)
                {
                    return false;
                }

                const RhiRenderPassColorAttachmentDesc& colorAttachment = desc.colorAttachments[0];
                activeSwapchain_ = nullptr;
                activeTexture_ = nullptr;
                activeDepthTexture_ = nullptr;
                ID3D12Resource* renderTarget = nullptr;
                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {};
                D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};

                if (colorAttachment.texture != nullptr)
                {
                    auto* d3dTexture = dynamic_cast<D3D12Texture*>(colorAttachment.texture);
                    if (d3dTexture == nullptr || !d3dTexture->HasRenderTargetView())
                    {
                        return false;
                    }

                    activeTexture_ = d3dTexture;
                    renderTarget = d3dTexture->GetNativeResource();
                    rtvHandle = d3dTexture->GetRenderTargetView();
                    TransitionResource(renderTarget, d3dTexture->GetResourceState(), D3D12_RESOURCE_STATE_RENDER_TARGET);
                    d3dTexture->SetResourceState(D3D12_RESOURCE_STATE_RENDER_TARGET);
                }
                else
                {
                    activeSwapchain_ = d3dSwapchain;
                    renderTarget = d3dSwapchain->GetCurrentRenderTarget();
                    rtvHandle = d3dSwapchain->GetCurrentRtv();
                    TransitionResource(renderTarget, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
                }

                if (desc.hasDepthStencilAttachment)
                {
                    auto* d3dDepthTexture = dynamic_cast<D3D12Texture*>(desc.depthStencilAttachment.texture);
                    if (d3dDepthTexture == nullptr || !d3dDepthTexture->HasDepthStencilView())
                    {
                        return false;
                    }

                    activeDepthTexture_ = d3dDepthTexture;
                    dsvHandle = d3dDepthTexture->GetDepthStencilView();
                    TransitionResource(d3dDepthTexture->GetNativeResource(), d3dDepthTexture->GetResourceState(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
                    d3dDepthTexture->SetResourceState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
                }

                commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, desc.hasDepthStencilAttachment ? &dsvHandle : nullptr);

                if (colorAttachment.loadAction == RhiLoadAction::Clear)
                {
                    const float clearColor[4] = {
                        colorAttachment.clearColor.r, colorAttachment.clearColor.g, colorAttachment.clearColor.b, colorAttachment.clearColor.a};
                    commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
                }

                if (desc.hasDepthStencilAttachment && desc.depthStencilAttachment.depthLoadAction == RhiLoadAction::Clear)
                {
                    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, desc.depthStencilAttachment.clearValue.depth, 0, 0, nullptr);
                }

                return true;
            }

            void EndRenderPass() override
            {
                if (activeSwapchain_ != nullptr)
                {
                    TransitionResource(activeSwapchain_->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
                    activeSwapchain_ = nullptr;
                }

                activeTexture_ = nullptr;
                activeDepthTexture_ = nullptr;
            }

            [[nodiscard]] bool CopyTextureToSwapchain(RhiTexture& sourceTexture, RhiSwapchain& swapchain) override
            {
                auto* d3dTexture = dynamic_cast<D3D12Texture*>(&sourceTexture);
                auto* d3dSwapchain = dynamic_cast<D3D12Swapchain*>(&swapchain);
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

                TransitionResource(d3dTexture->GetNativeResource(), d3dTexture->GetResourceState(), D3D12_RESOURCE_STATE_COPY_SOURCE);
                d3dTexture->SetResourceState(D3D12_RESOURCE_STATE_COPY_SOURCE);
                TransitionResource(d3dSwapchain->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
                commandList_->CopyResource(d3dSwapchain->GetCurrentRenderTarget(), d3dTexture->GetNativeResource());
                TransitionResource(d3dSwapchain->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
                return true;
            }

            void SetPipeline(const RhiPipelineState& pipelineState) override
            {
                const auto& d3dPipeline = static_cast<const D3D12PipelineState&>(pipelineState);
                commandList_->SetGraphicsRootSignature(d3dPipeline.GetRootSignature());
                commandList_->SetPipelineState(d3dPipeline.GetPipelineState());
                commandList_->IASetPrimitiveTopology(ToD3DTopology(d3dPipeline.GetTopology()));
            }

            void SetViewport(const RhiViewport& viewport) override
            {
                D3D12_VIEWPORT d3dViewport = {};
                d3dViewport.TopLeftX = viewport.x;
                d3dViewport.TopLeftY = viewport.y;
                d3dViewport.Width = viewport.width;
                d3dViewport.Height = viewport.height;
                d3dViewport.MinDepth = viewport.minDepth;
                d3dViewport.MaxDepth = viewport.maxDepth;
                commandList_->RSSetViewports(1, &d3dViewport);
            }

            void SetScissor(const RhiScissorRect& scissorRect) override
            {
                D3D12_RECT d3dRect = {};
                d3dRect.left = scissorRect.x;
                d3dRect.top = scissorRect.y;
                d3dRect.right = scissorRect.x + static_cast<LONG>(scissorRect.width);
                d3dRect.bottom = scissorRect.y + static_cast<LONG>(scissorRect.height);
                commandList_->RSSetScissorRects(1, &d3dRect);
            }

            void SetVertexBuffer(uint32_t slot, const RhiBuffer& buffer, uint32_t stride, uint64_t offset) override
            {
                const auto& d3dBuffer = static_cast<const D3D12Buffer&>(buffer);

                D3D12_VERTEX_BUFFER_VIEW bufferView = {};
                bufferView.BufferLocation = d3dBuffer.GetNativeResource()->GetGPUVirtualAddress() + offset;
                bufferView.SizeInBytes = static_cast<UINT>(d3dBuffer.GetSize() - offset);
                bufferView.StrideInBytes = stride;

                commandList_->IASetVertexBuffers(slot, 1, &bufferView);
            }

            void SetIndexBuffer(const RhiBuffer& buffer, RhiIndexFormat format, uint64_t offset) override
            {
                const auto& d3dBuffer = static_cast<const D3D12Buffer&>(buffer);

                D3D12_INDEX_BUFFER_VIEW bufferView = {};
                bufferView.BufferLocation = d3dBuffer.GetNativeResource()->GetGPUVirtualAddress() + offset;
                bufferView.SizeInBytes = static_cast<UINT>(d3dBuffer.GetSize() - offset);
                bufferView.Format = format == RhiIndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

                commandList_->IASetIndexBuffer(&bufferView);
            }

            void SetUniformBuffer(RhiShaderStage stage, uint32_t slot, const RhiBuffer& buffer, uint64_t offset) override
            {
                const auto& d3dBuffer = static_cast<const D3D12Buffer&>(buffer);
                const UINT rootParameterIndex = ResolveUniformRootParameter(stage, slot);
                commandList_->SetGraphicsRootConstantBufferView(rootParameterIndex, d3dBuffer.GetNativeResource()->GetGPUVirtualAddress() + offset);
            }

            void Draw(uint32_t vertexCount, uint32_t firstVertex) override
            {
                commandList_->DrawInstanced(vertexCount, 1, firstVertex, 0);
            }

            void DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) override
            {
                commandList_->DrawIndexedInstanced(indexCount, 1, firstIndex, vertexOffset, 0);
            }

            [[nodiscard]] ID3D12GraphicsCommandList* GetNativeCommandList() const noexcept
            {
                return commandList_.Get();
            }

        private:
            void TransitionResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
            {
                if (resource == nullptr || before == after)
                {
                    return;
                }

                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = resource;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = before;
                barrier.Transition.StateAfter = after;
                commandList_->ResourceBarrier(1, &barrier);
            }

            [[nodiscard]] static UINT ResolveUniformRootParameter(RhiShaderStage stage, uint32_t slot) noexcept
            {
                VE_ASSERT((stage == RhiShaderStage::Vertex && slot == 0) || (stage == RhiShaderStage::Fragment && (slot == 1 || slot == 2)));
                if (stage == RhiShaderStage::Vertex)
                {
                    return 0u;
                }

                return slot == 1 ? 1u : 2u;
            }

            ComPtr<ID3D12Device> device_;
            ComPtr<ID3D12CommandAllocator> commandAllocator_;
            ComPtr<ID3D12GraphicsCommandList> commandList_;
            D3D12Swapchain* activeSwapchain_ = nullptr;
            D3D12Texture* activeTexture_ = nullptr;
            D3D12Texture* activeDepthTexture_ = nullptr;
        };

        class D3D12Device final : public RhiDevice
        {
        public:
            explicit D3D12Device(bool enableDebug)
                : enableDebug_(enableDebug)
            {
            }

            ~D3D12Device() override
            {
                WaitIdle();

                if (fenceEvent_ != nullptr)
                {
                    CloseHandle(fenceEvent_);
                    fenceEvent_ = nullptr;
                }
            }

            [[nodiscard]] bool Initialize()
            {
                UINT factoryFlags = 0;

                if (enableDebug_)
                {
                    ComPtr<ID3D12Debug> debugController;

                    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
                    {
                        debugController->EnableDebugLayer();
                        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
                    }
                }

                HRESULT result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));

                if (FAILED(result))
                {
                    factoryFlags = 0;
                    result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory_));
                }

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("CreateDXGIFactory2", result));
                    return false;
                }

                result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("D3D12CreateDevice", result));
                    return false;
                }

                D3D12_COMMAND_QUEUE_DESC queueDesc = {};
                queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

                result = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue_));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateCommandQueue", result));
                    return false;
                }

                result = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateFence", result));
                    return false;
                }

                fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);

                if (fenceEvent_ == nullptr)
                {
                    SetLastError("CreateEvent failed for D3D12 fence.");
                    return false;
                }

                return true;
            }

            [[nodiscard]] RhiBackend GetBackend() const noexcept override
            {
                return RhiBackend::D3D12;
            }

            [[nodiscard]] const char* GetLastErrorMessage() const noexcept override
            {
                return lastError_.c_str();
            }

            [[nodiscard]] void* GetNativeDeviceHandle() const noexcept override
            {
                return device_.Get();
            }

            [[nodiscard]] void* GetNativeGraphicsQueueHandle() const noexcept override
            {
                return queue_.Get();
            }

            [[nodiscard]] std::unique_ptr<RhiSwapchain> CreateSwapchain(const RhiSwapchainDesc& desc) override
            {
                HWND window = static_cast<HWND>(desc.nativeWindow);

                if (window == nullptr)
                {
                    SetLastError("D3D12 swapchain requires a native HWND.");
                    return nullptr;
                }

                DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
                swapchainDesc.Width = desc.width;
                swapchainDesc.Height = desc.height;
                swapchainDesc.Format = ToDxgiFormat(desc.colorFormat);
                swapchainDesc.SampleDesc.Count = 1;
                swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                swapchainDesc.BufferCount = std::max(desc.bufferCount, 2u);
                swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

                ComPtr<IDXGISwapChain1> swapchain;
                HRESULT result = factory_->CreateSwapChainForHwnd(queue_.Get(), window, &swapchainDesc, nullptr, nullptr, &swapchain);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("IDXGIFactory4::CreateSwapChainForHwnd", result));
                    return nullptr;
                }

                factory_->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

                ComPtr<IDXGISwapChain3> swapchain3;
                result = swapchain.As(&swapchain3);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("IDXGISwapChain1::QueryInterface IDXGISwapChain3", result));
                    return nullptr;
                }

                auto rhiSwapchain = std::make_unique<D3D12Swapchain>(
                    device_, swapchain3, RhiExtent2D{desc.width, desc.height}, desc.colorFormat, swapchainDesc.BufferCount, &lastError_);

                if (!rhiSwapchain->Initialize())
                {
                    return nullptr;
                }

                return rhiSwapchain;
            }

            [[nodiscard]] std::unique_ptr<RhiBuffer> CreateBuffer(const RhiBufferDesc& desc) override
            {
                D3D12_HEAP_PROPERTIES heapProperties = {};
                heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
                heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
                heapProperties.CreationNodeMask = 1;
                heapProperties.VisibleNodeMask = 1;

                D3D12_RESOURCE_DESC resourceDesc = {};
                resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                resourceDesc.Alignment = 0;
                resourceDesc.Width = desc.size;
                resourceDesc.Height = 1;
                resourceDesc.DepthOrArraySize = 1;
                resourceDesc.MipLevels = 1;
                resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
                resourceDesc.SampleDesc.Count = 1;
                resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

                ComPtr<ID3D12Resource> resource;
                HRESULT result = device_->CreateCommittedResource(
                    &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateCommittedResource buffer", result));
                    return nullptr;
                }

                if (desc.initialData != nullptr && desc.size > 0)
                {
                    void* mappedData = nullptr;
                    D3D12_RANGE readRange = {};
                    result = resource->Map(0, &readRange, &mappedData);

                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Resource::Map", result));
                        return nullptr;
                    }

                    std::memcpy(mappedData, desc.initialData, static_cast<size_t>(desc.size));
                    resource->Unmap(0, nullptr);
                }

                return std::make_unique<D3D12Buffer>(resource, desc.size);
            }

            [[nodiscard]] std::unique_ptr<RhiTexture> CreateTexture(const RhiTextureDesc& desc) override
            {
                if (desc.dimension != RhiTextureDimension::Texture2D || desc.width == 0 || desc.height == 0)
                {
                    SetLastError("D3D12 texture requires a non-empty 2D descriptor.");
                    return nullptr;
                }

                D3D12_HEAP_PROPERTIES heapProperties = {};
                heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
                heapProperties.CreationNodeMask = 1;
                heapProperties.VisibleNodeMask = 1;

                D3D12_RESOURCE_DESC resourceDesc = {};
                resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                resourceDesc.Width = desc.width;
                resourceDesc.Height = desc.height;
                resourceDesc.DepthOrArraySize = 1;
                resourceDesc.MipLevels = static_cast<UINT16>(desc.mipLevelCount);
                resourceDesc.Format = ToDxgiFormat(desc.format);
                resourceDesc.SampleDesc.Count = 1;
                resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                resourceDesc.Flags = ToD3D12TextureFlags(desc.usage);

                D3D12_CLEAR_VALUE clearValue = {};
                D3D12_CLEAR_VALUE* clearValuePtr = nullptr;
                const auto usageValue = static_cast<uint32_t>(desc.usage);
                if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::DepthStencil)) != 0)
                {
                    clearValue.Format = ToDxgiFormat(desc.format);
                    clearValue.DepthStencil.Depth = 1.0f;
                    clearValue.DepthStencil.Stencil = 0;
                    clearValuePtr = &clearValue;
                }

                ComPtr<ID3D12Resource> resource;
                HRESULT result = device_->CreateCommittedResource(
                    &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, clearValuePtr, IID_PPV_ARGS(&resource));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateCommittedResource texture", result));
                    return nullptr;
                }

                if (desc.initialData != nullptr && desc.initialDataSize > 0)
                {
                    SetLastError("D3D12 texture initial data upload is not implemented in the minimum RHI path.");
                    return nullptr;
                }

                ComPtr<ID3D12DescriptorHeap> rtvHeap;
                if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::RenderTarget)) != 0)
                {
                    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
                    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                    heapDesc.NumDescriptors = 1;
                    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

                    result = device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap));
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Device::CreateDescriptorHeap texture RTV", result));
                        return nullptr;
                    }

                    device_->CreateRenderTargetView(resource.Get(), nullptr, rtvHeap->GetCPUDescriptorHandleForHeapStart());
                }

                ComPtr<ID3D12DescriptorHeap> dsvHeap;
                if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::DepthStencil)) != 0)
                {
                    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
                    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
                    heapDesc.NumDescriptors = 1;
                    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

                    result = device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsvHeap));
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Device::CreateDescriptorHeap texture DSV", result));
                        return nullptr;
                    }

                    device_->CreateDepthStencilView(resource.Get(), nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());
                }

                return std::make_unique<D3D12Texture>(resource, rtvHeap, dsvHeap, desc);
            }

            [[nodiscard]] std::unique_ptr<RhiShaderModule> CreateShaderModule(const RhiShaderModuleDesc& desc) override
            {
                if (desc.source == nullptr || desc.entryPoint == nullptr)
                {
                    SetLastError("D3D12 shader module requires source and entry point.");
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

                return std::make_unique<D3D12ShaderModule>(desc.stage, bytecode);
            }

            [[nodiscard]] std::unique_ptr<RhiPipelineState> CreateGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) override
            {
                const auto* vertexShaderModule = dynamic_cast<const D3D12ShaderModule*>(desc.vertexShader);
                const auto* fragmentShaderModule = dynamic_cast<const D3D12ShaderModule*>(desc.fragmentShader);

                if (vertexShaderModule == nullptr || fragmentShaderModule == nullptr)
                {
                    SetLastError("D3D12 graphics pipeline requires D3D12 shader modules.");
                    return nullptr;
                }

                D3D12_ROOT_PARAMETER rootParameters[3] = {};
                rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                rootParameters[0].Descriptor.ShaderRegister = 0;
                rootParameters[0].Descriptor.RegisterSpace = 0;
                rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
                rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                rootParameters[1].Descriptor.ShaderRegister = 1;
                rootParameters[1].Descriptor.RegisterSpace = 0;
                rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
                rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                rootParameters[2].Descriptor.ShaderRegister = 2;
                rootParameters[2].Descriptor.RegisterSpace = 0;
                rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

                D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
                rootSignatureDesc.NumParameters = 3;
                rootSignatureDesc.pParameters = rootParameters;
                rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

                ComPtr<ID3DBlob> signature;
                ComPtr<ID3DBlob> errors;
                HRESULT result = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);

                if (FAILED(result))
                {
                    if (errors != nullptr)
                    {
                        lastError_.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
                    }
                    else
                    {
                        SetLastError(MakeHResultError("D3D12SerializeRootSignature", result));
                    }

                    return nullptr;
                }

                ComPtr<ID3D12RootSignature> rootSignature;
                result = device_->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateRootSignature", result));
                    return nullptr;
                }

                std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
                inputElements.reserve(desc.vertexLayout.attributeCount);

                for (uint32_t index = 0; index < desc.vertexLayout.attributeCount; ++index)
                {
                    const RhiVertexAttributeDesc& attribute = desc.vertexLayout.attributes[index];
                    D3D12_INPUT_ELEMENT_DESC inputElement = {};
                    inputElement.SemanticName = attribute.semanticName;
                    inputElement.SemanticIndex = attribute.semanticIndex;
                    inputElement.Format = ToDxgiFormat(attribute.format);
                    inputElement.InputSlot = 0;
                    inputElement.AlignedByteOffset = attribute.offset;
                    inputElement.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                    inputElement.InstanceDataStepRate = 0;
                    inputElements.push_back(inputElement);
                }

                D3D12_RASTERIZER_DESC rasterizerDesc = {};
                rasterizerDesc.FillMode = desc.fillMode == RhiFillMode::Wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
                rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
                rasterizerDesc.FrontCounterClockwise = FALSE;
                rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
                rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
                rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
                rasterizerDesc.DepthClipEnable = TRUE;
                rasterizerDesc.MultisampleEnable = FALSE;
                rasterizerDesc.AntialiasedLineEnable = FALSE;
                rasterizerDesc.ForcedSampleCount = 0;
                rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

                D3D12_BLEND_DESC blendDesc = {};
                blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

                D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
                depthStencilDesc.DepthEnable = desc.depthTestEnabled ? TRUE : FALSE;
                depthStencilDesc.DepthWriteMask = desc.depthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
                depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
                depthStencilDesc.StencilEnable = FALSE;

                D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = {};
                pipelineDesc.pRootSignature = rootSignature.Get();
                pipelineDesc.VS = vertexShaderModule->GetBytecode();
                pipelineDesc.PS = fragmentShaderModule->GetBytecode();
                pipelineDesc.BlendState = blendDesc;
                pipelineDesc.SampleMask = UINT_MAX;
                pipelineDesc.RasterizerState = rasterizerDesc;
                pipelineDesc.DepthStencilState = depthStencilDesc;
                pipelineDesc.InputLayout = {inputElements.data(), static_cast<UINT>(inputElements.size())};
                pipelineDesc.PrimitiveTopologyType = ToD3D12TopologyType(desc.topology);
                pipelineDesc.NumRenderTargets = 1;
                pipelineDesc.RTVFormats[0] = ToDxgiFormat(desc.colorFormat);
                pipelineDesc.DSVFormat = desc.depthTestEnabled ? ToDxgiFormat(desc.depthFormat) : DXGI_FORMAT_UNKNOWN;
                pipelineDesc.SampleDesc.Count = 1;

                ComPtr<ID3D12PipelineState> pipelineState;
                result = device_->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&pipelineState));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateGraphicsPipelineState", result));
                    return nullptr;
                }

                return std::make_unique<D3D12PipelineState>(desc.topology, rootSignature, pipelineState);
            }

            [[nodiscard]] std::unique_ptr<RhiCommandList> CreateCommandList() override
            {
                ComPtr<ID3D12CommandAllocator> commandAllocator;
                HRESULT result = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateCommandAllocator", result));
                    return nullptr;
                }

                ComPtr<ID3D12GraphicsCommandList> commandList;
                result = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateCommandList", result));
                    return nullptr;
                }

                commandList->Close();
                return std::make_unique<D3D12CommandList>(device_, commandAllocator, commandList);
            }

            [[nodiscard]] std::unique_ptr<RhiFence> CreateFence(uint64_t initialValue = 0) override
            {
                ComPtr<ID3D12Fence> fence;
                HRESULT result = device_->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateFence", result));
                    return nullptr;
                }

                return std::make_unique<D3D12FenceObject>(fence, fenceEvent_);
            }

            [[nodiscard]] bool SignalFence(RhiFence& fence, uint64_t value) override
            {
                auto* d3dFence = dynamic_cast<D3D12FenceObject*>(&fence);
                if (d3dFence == nullptr)
                {
                    SetLastError("D3D12 device can only signal D3D12 fences.");
                    return false;
                }

                HRESULT result = queue_->Signal(d3dFence->GetNativeFence(), value);
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12CommandQueue::Signal external fence", result));
                    return false;
                }

                return true;
            }

            [[nodiscard]] bool Submit(RhiCommandList& commandList) override
            {
                auto* d3dCommandList = dynamic_cast<D3D12CommandList*>(&commandList);

                if (d3dCommandList == nullptr)
                {
                    SetLastError("D3D12 device can only submit D3D12 command lists.");
                    return false;
                }

                ID3D12CommandList* nativeCommandLists[] = {d3dCommandList->GetNativeCommandList()};
                queue_->ExecuteCommandLists(1, nativeCommandLists);
                return SignalAndWait();
            }

            void WaitIdle() override
            {
                if (queue_ != nullptr && fence_ != nullptr && fenceEvent_ != nullptr)
                {
                    (void)SignalAndWait();
                }
            }

        private:
            [[nodiscard]] bool SignalAndWait()
            {
                const uint64_t fenceValue = nextFenceValue_;
                ++nextFenceValue_;

                HRESULT result = queue_->Signal(fence_.Get(), fenceValue);

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12CommandQueue::Signal", result));
                    return false;
                }

                if (fence_->GetCompletedValue() < fenceValue)
                {
                    result = fence_->SetEventOnCompletion(fenceValue, fenceEvent_);

                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Fence::SetEventOnCompletion", result));
                        return false;
                    }

                    WaitForSingleObject(fenceEvent_, INFINITE);
                }

                return true;
            }

            void SetLastError(std::string error)
            {
                lastError_ = std::move(error);
            }

        private:
            bool enableDebug_ = false;
            ComPtr<IDXGIFactory4> factory_;
            ComPtr<ID3D12Device> device_;
            ComPtr<ID3D12CommandQueue> queue_;
            ComPtr<ID3D12Fence> fence_;
            HANDLE fenceEvent_ = nullptr;
            uint64_t nextFenceValue_ = 1;
            std::string lastError_;
        };
    } // namespace

    std::unique_ptr<RhiDevice> CreateD3D12Device(bool enableDebug)
    {
        auto device = std::make_unique<D3D12Device>(enableDebug);

        if (!device->Initialize())
        {
            return nullptr;
        }

        return device;
    }
} // namespace ve::rhi
