#include "Engine/RHI/D3D12/D3D12Rhi.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdint>
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
            case RhiFormat::Depth32Float:
                return DXGI_FORMAT_D32_FLOAT;
            case RhiFormat::Rg32Float:
                return DXGI_FORMAT_R32G32_FLOAT;
            case RhiFormat::Rgb32Float:
                return DXGI_FORMAT_R32G32B32_FLOAT;
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

        DXGI_FORMAT ToD3D12IndexFormat(RhiIndexFormat format)
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

        D3D12_CULL_MODE ToD3D12CullMode(RhiCullMode cullMode)
        {
            switch (cullMode)
            {
            case RhiCullMode::None:
                return D3D12_CULL_MODE_NONE;
            case RhiCullMode::Back:
            default:
                return D3D12_CULL_MODE_BACK;
            }
        }

        D3D12_FILL_MODE ToD3D12FillMode(RhiFillMode fillMode)
        {
            switch (fillMode)
            {
            case RhiFillMode::Wireframe:
                return D3D12_FILL_MODE_WIREFRAME;
            case RhiFillMode::Solid:
            default:
                return D3D12_FILL_MODE_SOLID;
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

        [[nodiscard]] bool HasBufferUsage(RhiBufferUsage usage, RhiBufferUsage flag) noexcept
        {
            return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(flag)) != 0;
        }

        [[nodiscard]] bool HasTextureUsage(RhiTextureUsage usage, RhiTextureUsage flag) noexcept
        {
            return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(flag)) != 0;
        }

        [[nodiscard]] uint64_t AlignUp(uint64_t value, uint64_t alignment) noexcept
        {
            return ((value + alignment) - 1) / alignment * alignment;
        }

        D3D12_SHADER_VISIBILITY ToD3D12ShaderVisibility(RhiShaderStage stage)
        {
            switch (stage)
            {
            case RhiShaderStage::Vertex:
                return D3D12_SHADER_VISIBILITY_VERTEX;
            case RhiShaderStage::Fragment:
                return D3D12_SHADER_VISIBILITY_PIXEL;
            }

            return D3D12_SHADER_VISIBILITY_ALL;
        }

        struct D3D12UniformBufferBinding
        {
            RhiShaderStage stage = RhiShaderStage::Vertex;
            uint32_t slot = 0;
            uint32_t rootParameterIndex = 0;
        };

        struct D3D12TextureBinding
        {
            RhiShaderStage stage = RhiShaderStage::Fragment;
            uint32_t slot = 0;
            uint32_t rootParameterIndex = 0;
        };

        std::string MakeHResultError(const char* operation, HRESULT result)
        {
            char buffer[128] = {};
            std::snprintf(
                buffer, sizeof(buffer), "%s failed with HRESULT 0x%08X", operation, static_cast<unsigned>(result));
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
                         ComPtr<ID3D12DescriptorHeap> shaderResourceHeap,
                         ComPtr<ID3D12DescriptorHeap> renderTargetHeap,
                         ComPtr<ID3D12DescriptorHeap> depthStencilHeap,
                         D3D12_RESOURCE_STATES resourceState,
                         RhiTextureDesc desc)
                : resource_(std::move(resource))
                , shaderResourceHeap_(std::move(shaderResourceHeap))
                , renderTargetHeap_(std::move(renderTargetHeap))
                , depthStencilHeap_(std::move(depthStencilHeap))
                , resourceState_(resourceState)
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

            [[nodiscard]] ID3D12DescriptorHeap* GetShaderResourceHeap() const noexcept
            {
                return shaderResourceHeap_.Get();
            }

            [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceHandle() const noexcept
            {
                if (shaderResourceHeap_ == nullptr)
                {
                    return {};
                }

                return shaderResourceHeap_->GetGPUDescriptorHandleForHeapStart();
            }

            [[nodiscard]] bool HasRenderTargetView() const noexcept
            {
                return renderTargetHeap_ != nullptr;
            }

            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetHandle() const noexcept
            {
                if (renderTargetHeap_ == nullptr)
                {
                    return {};
                }

                return renderTargetHeap_->GetCPUDescriptorHandleForHeapStart();
            }

            [[nodiscard]] bool HasDepthStencilView() const noexcept
            {
                return depthStencilHeap_ != nullptr;
            }

            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilHandle() const noexcept
            {
                if (depthStencilHeap_ == nullptr)
                {
                    return {};
                }

                return depthStencilHeap_->GetCPUDescriptorHandleForHeapStart();
            }

            [[nodiscard]] D3D12_RESOURCE_STATES GetResourceState() const noexcept
            {
                return resourceState_;
            }

            void SetResourceState(D3D12_RESOURCE_STATES resourceState) noexcept
            {
                resourceState_ = resourceState;
            }

            [[nodiscard]] bool IsSampled() const noexcept
            {
                const auto usageValue = static_cast<uint32_t>(desc_.usage);
                return (usageValue & static_cast<uint32_t>(RhiTextureUsage::Sampled)) != 0;
            }

        private:
            ComPtr<ID3D12Resource> resource_;
            ComPtr<ID3D12DescriptorHeap> shaderResourceHeap_;
            ComPtr<ID3D12DescriptorHeap> renderTargetHeap_;
            ComPtr<ID3D12DescriptorHeap> depthStencilHeap_;
            D3D12_RESOURCE_STATES resourceState_ = D3D12_RESOURCE_STATE_COMMON;
            RhiTextureDesc desc_ = {};
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
            D3D12PipelineState(RhiPrimitiveTopology topology,
                               ComPtr<ID3D12RootSignature> rootSignature,
                               ComPtr<ID3D12PipelineState> pipelineState,
                               std::vector<D3D12UniformBufferBinding> uniformBufferBindings,
                               std::vector<D3D12TextureBinding> textureBindings)
                : topology_(topology)
                , rootSignature_(std::move(rootSignature))
                , pipelineState_(std::move(pipelineState))
                , uniformBufferBindings_(std::move(uniformBufferBindings))
                , textureBindings_(std::move(textureBindings))
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

            [[nodiscard]] bool TryGetUniformBufferRootParameterIndex(RhiShaderStage stage,
                                                                     uint32_t slot,
                                                                     uint32_t& outIndex) const noexcept
            {
                for (const D3D12UniformBufferBinding& binding : uniformBufferBindings_)
                {
                    if (binding.stage == stage && binding.slot == slot)
                    {
                        outIndex = binding.rootParameterIndex;
                        return true;
                    }
                }

                return false;
            }

            [[nodiscard]] bool TryGetTextureRootParameterIndex(RhiShaderStage stage,
                                                               uint32_t slot,
                                                               uint32_t& outIndex) const noexcept
            {
                for (const D3D12TextureBinding& binding : textureBindings_)
                {
                    if (binding.stage == stage && binding.slot == slot)
                    {
                        outIndex = binding.rootParameterIndex;
                        return true;
                    }
                }

                return false;
            }

        private:
            RhiPrimitiveTopology topology_ = RhiPrimitiveTopology::TriangleList;
            ComPtr<ID3D12RootSignature> rootSignature_;
            ComPtr<ID3D12PipelineState> pipelineState_;
            std::vector<D3D12UniformBufferBinding> uniformBufferBindings_;
            std::vector<D3D12TextureBinding> textureBindings_;
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
            D3D12CommandList(ComPtr<ID3D12Device> device,
                             ComPtr<ID3D12CommandAllocator> commandAllocator,
                             ComPtr<ID3D12GraphicsCommandList> commandList)
                : device_(std::move(device))
                , commandAllocator_(std::move(commandAllocator))
                , commandList_(std::move(commandList))
            {
            }

            [[nodiscard]] bool Begin() override
            {
                activePipelineState_ = nullptr;
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

                if (d3dSwapchain == nullptr)
                {
                    return false;
                }

                activeSwapchain_ = d3dSwapchain;
                activeRenderTargetTexture_ = nullptr;
                activeDepthStencilTexture_ = PrepareDepthStencilAttachment(desc.depthStencilAttachment);

                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = d3dSwapchain->GetCurrentRenderTarget();
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                commandList_->ResourceBarrier(1, &barrier);

                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = d3dSwapchain->GetCurrentRtv();
                D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
                D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandlePtr = nullptr;
                if (activeDepthStencilTexture_ != nullptr)
                {
                    dsvHandle = activeDepthStencilTexture_->GetDepthStencilHandle();
                    dsvHandlePtr = &dsvHandle;
                }
                commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, dsvHandlePtr);

                if (desc.colorLoadAction == RhiLoadAction::Clear)
                {
                    const float clearColor[4] = {
                        desc.clearColor.r, desc.clearColor.g, desc.clearColor.b, desc.clearColor.a};
                    commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
                }

                if (dsvHandlePtr != nullptr && desc.depthLoadAction == RhiLoadAction::Clear)
                {
                    commandList_->ClearDepthStencilView(*dsvHandlePtr,
                                                        D3D12_CLEAR_FLAG_DEPTH,
                                                        desc.clearDepth,
                                                        0,
                                                        0,
                                                        nullptr);
                }

                return true;
            }

            [[nodiscard]] bool BeginRenderPass(RhiTexture& texture, const RhiRenderPassDesc& desc) override
            {
                auto* d3dTexture = dynamic_cast<D3D12Texture*>(&texture);
                if (d3dTexture == nullptr || !d3dTexture->HasRenderTargetView())
                {
                    return false;
                }

                activeSwapchain_ = nullptr;
                activeRenderTargetTexture_ = d3dTexture;
                activeDepthStencilTexture_ = PrepareDepthStencilAttachment(desc.depthStencilAttachment);

                const D3D12_RESOURCE_STATES beforeState = d3dTexture->GetResourceState();
                if (beforeState != D3D12_RESOURCE_STATE_RENDER_TARGET)
                {
                    D3D12_RESOURCE_BARRIER barrier = {};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.Transition.pResource = d3dTexture->GetNativeResource();
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    barrier.Transition.StateBefore = beforeState;
                    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    commandList_->ResourceBarrier(1, &barrier);
                    d3dTexture->SetResourceState(D3D12_RESOURCE_STATE_RENDER_TARGET);
                }

                D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = d3dTexture->GetRenderTargetHandle();
                D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
                D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandlePtr = nullptr;
                if (activeDepthStencilTexture_ != nullptr)
                {
                    dsvHandle = activeDepthStencilTexture_->GetDepthStencilHandle();
                    dsvHandlePtr = &dsvHandle;
                }
                commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, dsvHandlePtr);

                if (desc.colorLoadAction == RhiLoadAction::Clear)
                {
                    const float clearColor[4] = {
                        desc.clearColor.r, desc.clearColor.g, desc.clearColor.b, desc.clearColor.a};
                    commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
                }

                if (dsvHandlePtr != nullptr && desc.depthLoadAction == RhiLoadAction::Clear)
                {
                    commandList_->ClearDepthStencilView(*dsvHandlePtr,
                                                        D3D12_CLEAR_FLAG_DEPTH,
                                                        desc.clearDepth,
                                                        0,
                                                        0,
                                                        nullptr);
                }

                return true;
            }

            void EndRenderPass() override
            {
                activeDepthStencilTexture_ = nullptr;

                if (activeRenderTargetTexture_ != nullptr)
                {
                    const D3D12_RESOURCE_STATES afterState = activeRenderTargetTexture_->IsSampled()
                                                                 ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                                                                 : D3D12_RESOURCE_STATE_COMMON;
                    const D3D12_RESOURCE_STATES beforeState = activeRenderTargetTexture_->GetResourceState();
                    if (beforeState != afterState)
                    {
                        D3D12_RESOURCE_BARRIER barrier = {};
                        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                        barrier.Transition.pResource = activeRenderTargetTexture_->GetNativeResource();
                        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                        barrier.Transition.StateBefore = beforeState;
                        barrier.Transition.StateAfter = afterState;
                        commandList_->ResourceBarrier(1, &barrier);
                        activeRenderTargetTexture_->SetResourceState(afterState);
                    }

                    activeRenderTargetTexture_ = nullptr;
                    return;
                }

                if (activeSwapchain_ == nullptr)
                {
                    return;
                }

                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.Transition.pResource = activeSwapchain_->GetCurrentRenderTarget();
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                commandList_->ResourceBarrier(1, &barrier);

                activeSwapchain_ = nullptr;
            }

            void SetPipeline(const RhiPipelineState& pipelineState) override
            {
                const auto& d3dPipeline = static_cast<const D3D12PipelineState&>(pipelineState);
                activePipelineState_ = &d3dPipeline;
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
                bufferView.Format = ToD3D12IndexFormat(format);

                commandList_->IASetIndexBuffer(&bufferView);
            }

            void SetUniformBuffer(RhiShaderStage stage,
                                  uint32_t slot,
                                  const RhiBuffer& buffer,
                                  uint64_t offset,
                                  uint64_t size) override
            {
                (void)size;

                if (activePipelineState_ == nullptr)
                {
                    return;
                }

                uint32_t rootParameterIndex = 0;
                if (!activePipelineState_->TryGetUniformBufferRootParameterIndex(stage, slot, rootParameterIndex))
                {
                    return;
                }

                const auto& d3dBuffer = static_cast<const D3D12Buffer&>(buffer);
                commandList_->SetGraphicsRootConstantBufferView(
                    rootParameterIndex, d3dBuffer.GetNativeResource()->GetGPUVirtualAddress() + offset);
            }

            void SetTexture(RhiShaderStage stage, uint32_t slot, const RhiTexture& texture) override
            {
                if (activePipelineState_ == nullptr)
                {
                    return;
                }

                uint32_t rootParameterIndex = 0;
                if (!activePipelineState_->TryGetTextureRootParameterIndex(stage, slot, rootParameterIndex))
                {
                    return;
                }

                const auto& d3dTexture = static_cast<const D3D12Texture&>(texture);
                ID3D12DescriptorHeap* heaps[] = {d3dTexture.GetShaderResourceHeap()};
                if (heaps[0] == nullptr)
                {
                    return;
                }

                commandList_->SetDescriptorHeaps(1, heaps);
                commandList_->SetGraphicsRootDescriptorTable(rootParameterIndex, d3dTexture.GetShaderResourceHandle());
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
            [[nodiscard]] D3D12Texture* PrepareDepthStencilAttachment(RhiTexture* texture)
            {
                auto* d3dTexture = dynamic_cast<D3D12Texture*>(texture);
                if (d3dTexture == nullptr || !d3dTexture->HasDepthStencilView())
                {
                    return nullptr;
                }

                const D3D12_RESOURCE_STATES beforeState = d3dTexture->GetResourceState();
                if (beforeState != D3D12_RESOURCE_STATE_DEPTH_WRITE)
                {
                    D3D12_RESOURCE_BARRIER barrier = {};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                    barrier.Transition.pResource = d3dTexture->GetNativeResource();
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    barrier.Transition.StateBefore = beforeState;
                    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
                    commandList_->ResourceBarrier(1, &barrier);
                    d3dTexture->SetResourceState(D3D12_RESOURCE_STATE_DEPTH_WRITE);
                }

                return d3dTexture;
            }

            ComPtr<ID3D12Device> device_;
            ComPtr<ID3D12CommandAllocator> commandAllocator_;
            ComPtr<ID3D12GraphicsCommandList> commandList_;
            D3D12Swapchain* activeSwapchain_ = nullptr;
            D3D12Texture* activeRenderTargetTexture_ = nullptr;
            D3D12Texture* activeDepthStencilTexture_ = nullptr;
            const D3D12PipelineState* activePipelineState_ = nullptr;
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
                HRESULT result = factory_->CreateSwapChainForHwnd(
                    queue_.Get(), window, &swapchainDesc, nullptr, nullptr, &swapchain);

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

                auto rhiSwapchain = std::make_unique<D3D12Swapchain>(device_,
                                                                     swapchain3,
                                                                     RhiExtent2D{desc.width, desc.height},
                                                                     desc.colorFormat,
                                                                     swapchainDesc.BufferCount,
                                                                     &lastError_);

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
                    SetLastError("D3D12 buffer requires a non-zero size.");
                    return nullptr;
                }

                D3D12_HEAP_PROPERTIES heapProperties = {};
                heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
                heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
                heapProperties.CreationNodeMask = 1;
                heapProperties.VisibleNodeMask = 1;

                D3D12_RESOURCE_DESC resourceDesc = {};
                resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                resourceDesc.Alignment = 0;
                resourceDesc.Width = HasBufferUsage(desc.usage, RhiBufferUsage::Uniform)
                                         ? AlignUp(desc.size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)
                                         : desc.size;
                resourceDesc.Height = 1;
                resourceDesc.DepthOrArraySize = 1;
                resourceDesc.MipLevels = 1;
                resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
                resourceDesc.SampleDesc.Count = 1;
                resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

                ComPtr<ID3D12Resource> resource;
                HRESULT result = device_->CreateCommittedResource(&heapProperties,
                                                                  D3D12_HEAP_FLAG_NONE,
                                                                  &resourceDesc,
                                                                  D3D12_RESOURCE_STATE_GENERIC_READ,
                                                                  nullptr,
                                                                  IID_PPV_ARGS(&resource));

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

            [[nodiscard]] bool UpdateBuffer(RhiBuffer& buffer,
                                            const void* data,
                                            uint64_t size,
                                            uint64_t offset = 0) override
            {
                auto* d3dBuffer = dynamic_cast<D3D12Buffer*>(&buffer);
                if (d3dBuffer == nullptr || data == nullptr || size == 0 || offset + size > d3dBuffer->GetSize())
                {
                    SetLastError("D3D12 buffer update received invalid data or range.");
                    return false;
                }

                void* mappedData = nullptr;
                D3D12_RANGE readRange = {};
                HRESULT result = d3dBuffer->GetNativeResource()->Map(0, &readRange, &mappedData);
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Resource::Map buffer update", result));
                    return false;
                }

                std::memcpy(static_cast<uint8_t*>(mappedData) + offset, data, static_cast<size_t>(size));

                D3D12_RANGE writtenRange = {};
                writtenRange.Begin = static_cast<SIZE_T>(offset);
                writtenRange.End = static_cast<SIZE_T>(offset + size);
                d3dBuffer->GetNativeResource()->Unmap(0, &writtenRange);
                return true;
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

                const bool hasInitialData = desc.initialData != nullptr && desc.initialDataSize > 0;
                const bool isSampled = HasTextureUsage(desc.usage, RhiTextureUsage::Sampled);
                const bool isDepthStencil = HasTextureUsage(desc.usage, RhiTextureUsage::DepthStencil);
                D3D12_RESOURCE_STATES initialState = hasInitialData ? D3D12_RESOURCE_STATE_COPY_DEST
                                                                    : D3D12_RESOURCE_STATE_COMMON;
                if (!hasInitialData && isSampled && !HasTextureUsage(desc.usage, RhiTextureUsage::RenderTarget))
                {
                    initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                }
                if (!hasInitialData && isDepthStencil)
                {
                    initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
                }
                D3D12_RESOURCE_STATES resourceState = initialState;

                D3D12_CLEAR_VALUE clearValue = {};
                D3D12_CLEAR_VALUE* clearValuePtr = nullptr;
                if (isDepthStencil)
                {
                    clearValue.Format = resourceDesc.Format;
                    clearValue.DepthStencil.Depth = 1.0f;
                    clearValue.DepthStencil.Stencil = 0;
                    clearValuePtr = &clearValue;
                }

                ComPtr<ID3D12Resource> resource;
                HRESULT result = device_->CreateCommittedResource(&heapProperties,
                                                                  D3D12_HEAP_FLAG_NONE,
                                                                  &resourceDesc,
                                                                  initialState,
                                                                  clearValuePtr,
                                                                  IID_PPV_ARGS(&resource));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateCommittedResource texture", result));
                    return nullptr;
                }

                if (hasInitialData)
                {
                    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
                    UINT rowCount = 0;
                    UINT64 rowSizeInBytes = 0;
                    UINT64 uploadBufferSize = 0;
                    device_->GetCopyableFootprints(
                        &resourceDesc, 0, 1, 0, &footprint, &rowCount, &rowSizeInBytes, &uploadBufferSize);

                    D3D12_HEAP_PROPERTIES uploadHeapProperties = {};
                    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
                    uploadHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                    uploadHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
                    uploadHeapProperties.CreationNodeMask = 1;
                    uploadHeapProperties.VisibleNodeMask = 1;

                    D3D12_RESOURCE_DESC uploadResourceDesc = {};
                    uploadResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                    uploadResourceDesc.Width = uploadBufferSize;
                    uploadResourceDesc.Height = 1;
                    uploadResourceDesc.DepthOrArraySize = 1;
                    uploadResourceDesc.MipLevels = 1;
                    uploadResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
                    uploadResourceDesc.SampleDesc.Count = 1;
                    uploadResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

                    ComPtr<ID3D12Resource> uploadResource;
                    result = device_->CreateCommittedResource(&uploadHeapProperties,
                                                              D3D12_HEAP_FLAG_NONE,
                                                              &uploadResourceDesc,
                                                              D3D12_RESOURCE_STATE_GENERIC_READ,
                                                              nullptr,
                                                              IID_PPV_ARGS(&uploadResource));

                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Device::CreateCommittedResource texture upload", result));
                        return nullptr;
                    }

                    void* mappedData = nullptr;
                    D3D12_RANGE readRange = {};
                    result = uploadResource->Map(0, &readRange, &mappedData);
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Resource::Map texture upload", result));
                        return nullptr;
                    }

                    const auto* sourceBytes = static_cast<const uint8_t*>(desc.initialData);
                    auto* destinationBytes = static_cast<uint8_t*>(mappedData) + footprint.Offset;
                    const uint64_t sourceRowPitch = desc.initialDataRowPitch != 0 ? desc.initialDataRowPitch
                                                                                  : rowSizeInBytes;
                    const uint64_t copyRowBytes = std::min<uint64_t>(sourceRowPitch, rowSizeInBytes);
                    for (UINT row = 0; row < rowCount; ++row)
                    {
                        std::memcpy(destinationBytes + (static_cast<uint64_t>(row) * footprint.Footprint.RowPitch),
                                    sourceBytes + (static_cast<uint64_t>(row) * sourceRowPitch),
                                    static_cast<size_t>(copyRowBytes));
                    }
                    uploadResource->Unmap(0, nullptr);

                    ComPtr<ID3D12CommandAllocator> uploadCommandAllocator;
                    result = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                             IID_PPV_ARGS(&uploadCommandAllocator));
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Device::CreateCommandAllocator texture upload", result));
                        return nullptr;
                    }

                    ComPtr<ID3D12GraphicsCommandList> uploadCommandList;
                    result = device_->CreateCommandList(0,
                                                        D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                        uploadCommandAllocator.Get(),
                                                        nullptr,
                                                        IID_PPV_ARGS(&uploadCommandList));
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Device::CreateCommandList texture upload", result));
                        return nullptr;
                    }

                    D3D12_TEXTURE_COPY_LOCATION destination = {};
                    destination.pResource = resource.Get();
                    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    destination.SubresourceIndex = 0;

                    D3D12_TEXTURE_COPY_LOCATION source = {};
                    source.pResource = uploadResource.Get();
                    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    source.PlacedFootprint = footprint;

                    uploadCommandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

                    const D3D12_RESOURCE_STATES finalState =
                        (isSampled && !HasTextureUsage(desc.usage, RhiTextureUsage::RenderTarget))
                            ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                            : D3D12_RESOURCE_STATE_COMMON;
                    if (finalState != D3D12_RESOURCE_STATE_COPY_DEST)
                    {
                        D3D12_RESOURCE_BARRIER barrier = {};
                        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        barrier.Transition.pResource = resource.Get();
                        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                        barrier.Transition.StateAfter = finalState;
                        uploadCommandList->ResourceBarrier(1, &barrier);
                    }

                    result = uploadCommandList->Close();
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12GraphicsCommandList::Close texture upload", result));
                        return nullptr;
                    }

                    ID3D12CommandList* commandLists[] = {uploadCommandList.Get()};
                    queue_->ExecuteCommandLists(1, commandLists);
                    if (!SignalAndWait())
                    {
                        return nullptr;
                    }
                    resourceState = finalState;
                }

                ComPtr<ID3D12DescriptorHeap> shaderResourceHeap;
                if (isSampled)
                {
                    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
                    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                    heapDesc.NumDescriptors = 1;
                    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

                    result = device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&shaderResourceHeap));
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Device::CreateDescriptorHeap SRV", result));
                        return nullptr;
                    }

                    D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc = {};
                    shaderResourceViewDesc.Format = resourceDesc.Format;
                    shaderResourceViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    shaderResourceViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    shaderResourceViewDesc.Texture2D.MipLevels = resourceDesc.MipLevels;

                    device_->CreateShaderResourceView(resource.Get(),
                                                      &shaderResourceViewDesc,
                                                      shaderResourceHeap->GetCPUDescriptorHandleForHeapStart());
                }

                ComPtr<ID3D12DescriptorHeap> renderTargetHeap;
                if (HasTextureUsage(desc.usage, RhiTextureUsage::RenderTarget))
                {
                    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
                    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                    heapDesc.NumDescriptors = 1;
                    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

                    result = device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&renderTargetHeap));
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Device::CreateDescriptorHeap RTV", result));
                        return nullptr;
                    }

                    device_->CreateRenderTargetView(
                        resource.Get(), nullptr, renderTargetHeap->GetCPUDescriptorHandleForHeapStart());
                }

                ComPtr<ID3D12DescriptorHeap> depthStencilHeap;
                if (isDepthStencil)
                {
                    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
                    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
                    heapDesc.NumDescriptors = 1;
                    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

                    result = device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&depthStencilHeap));
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Device::CreateDescriptorHeap DSV", result));
                        return nullptr;
                    }

                    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc = {};
                    depthStencilViewDesc.Format = resourceDesc.Format;
                    depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                    depthStencilViewDesc.Flags = D3D12_DSV_FLAG_NONE;
                    device_->CreateDepthStencilView(
                        resource.Get(), &depthStencilViewDesc, depthStencilHeap->GetCPUDescriptorHandleForHeapStart());
                }

                return std::make_unique<D3D12Texture>(
                    resource, shaderResourceHeap, renderTargetHeap, depthStencilHeap, resourceState, desc);
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

                return std::make_unique<D3D12ShaderModule>(desc.stage, bytecode);
            }

            [[nodiscard]] std::unique_ptr<RhiPipelineState>
            CreateGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) override
            {
                const auto* vertexShaderModule = dynamic_cast<const D3D12ShaderModule*>(desc.vertexShader);
                const auto* fragmentShaderModule = dynamic_cast<const D3D12ShaderModule*>(desc.fragmentShader);

                if (vertexShaderModule == nullptr || fragmentShaderModule == nullptr)
                {
                    SetLastError("D3D12 graphics pipeline requires D3D12 shader modules.");
                    return nullptr;
                }

                if (desc.uniformBufferBindingCount > 0 && desc.uniformBufferBindings == nullptr)
                {
                    SetLastError("D3D12 graphics pipeline uniform buffer binding count requires binding data.");
                    return nullptr;
                }

                if (desc.textureBindingCount > 0 && desc.textureBindings == nullptr)
                {
                    SetLastError("D3D12 graphics pipeline texture binding count requires binding data.");
                    return nullptr;
                }

                std::vector<D3D12_ROOT_PARAMETER> rootParameters;
                rootParameters.reserve(desc.uniformBufferBindingCount + desc.textureBindingCount);

                std::vector<D3D12UniformBufferBinding> uniformBufferBindings;
                uniformBufferBindings.reserve(desc.uniformBufferBindingCount);

                std::vector<D3D12TextureBinding> textureBindings;
                textureBindings.reserve(desc.textureBindingCount);

                std::vector<D3D12_DESCRIPTOR_RANGE> textureRanges;
                textureRanges.reserve(desc.textureBindingCount);

                std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;
                staticSamplers.reserve(desc.textureBindingCount);

                for (uint32_t index = 0; index < desc.uniformBufferBindingCount; ++index)
                {
                    const RhiUniformBufferBindingDesc& binding = desc.uniformBufferBindings[index];

                    D3D12_ROOT_PARAMETER rootParameter = {};
                    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                    rootParameter.Descriptor.ShaderRegister = binding.slot;
                    rootParameter.Descriptor.RegisterSpace = 0;
                    rootParameter.ShaderVisibility = ToD3D12ShaderVisibility(binding.stage);

                    uniformBufferBindings.push_back(D3D12UniformBufferBinding{
                        binding.stage, binding.slot, static_cast<uint32_t>(rootParameters.size())});
                    rootParameters.push_back(rootParameter);
                }

                for (uint32_t index = 0; index < desc.textureBindingCount; ++index)
                {
                    const RhiTextureBindingDesc& binding = desc.textureBindings[index];

                    D3D12_DESCRIPTOR_RANGE range = {};
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    range.NumDescriptors = 1;
                    range.BaseShaderRegister = binding.slot;
                    range.RegisterSpace = 0;
                    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                    textureRanges.push_back(range);

                    D3D12_ROOT_PARAMETER rootParameter = {};
                    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    rootParameter.DescriptorTable.NumDescriptorRanges = 1;
                    rootParameter.DescriptorTable.pDescriptorRanges = &textureRanges.back();
                    rootParameter.ShaderVisibility = ToD3D12ShaderVisibility(binding.stage);

                    textureBindings.push_back(
                        D3D12TextureBinding{binding.stage, binding.slot, static_cast<uint32_t>(rootParameters.size())});
                    rootParameters.push_back(rootParameter);

                    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
                    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                    samplerDesc.MipLODBias = 0.0f;
                    samplerDesc.MaxAnisotropy = 1;
                    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
                    samplerDesc.MinLOD = 0.0f;
                    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
                    samplerDesc.ShaderRegister = binding.slot;
                    samplerDesc.RegisterSpace = 0;
                    samplerDesc.ShaderVisibility = ToD3D12ShaderVisibility(binding.stage);
                    staticSamplers.push_back(samplerDesc);
                }

                D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
                rootSignatureDesc.NumParameters = static_cast<UINT>(rootParameters.size());
                rootSignatureDesc.pParameters = rootParameters.empty() ? nullptr : rootParameters.data();
                rootSignatureDesc.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
                rootSignatureDesc.pStaticSamplers = staticSamplers.empty() ? nullptr : staticSamplers.data();
                rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

                ComPtr<ID3DBlob> signature;
                ComPtr<ID3DBlob> errors;
                HRESULT result =
                    D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);

                if (FAILED(result))
                {
                    if (errors != nullptr)
                    {
                        lastError_.assign(static_cast<const char*>(errors->GetBufferPointer()),
                                          errors->GetBufferSize());
                    }
                    else
                    {
                        SetLastError(MakeHResultError("D3D12SerializeRootSignature", result));
                    }

                    return nullptr;
                }

                ComPtr<ID3D12RootSignature> rootSignature;
                result = device_->CreateRootSignature(
                    0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature));

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
                rasterizerDesc.FillMode = ToD3D12FillMode(desc.fillMode);
                rasterizerDesc.CullMode = ToD3D12CullMode(desc.cullMode);
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
                if (desc.enableAlphaBlending)
                {
                    blendDesc.RenderTarget[0].BlendEnable = TRUE;
                    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
                    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
                    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
                }

                D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
                depthStencilDesc.DepthEnable = desc.enableDepthTest ? TRUE : FALSE;
                depthStencilDesc.DepthWriteMask =
                    desc.enableDepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
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
                pipelineDesc.DSVFormat = ToDxgiFormat(desc.depthStencilFormat);
                pipelineDesc.SampleDesc.Count = 1;

                ComPtr<ID3D12PipelineState> pipelineState;
                result = device_->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&pipelineState));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateGraphicsPipelineState", result));
                    return nullptr;
                }

                return std::make_unique<D3D12PipelineState>(desc.topology,
                                                            rootSignature,
                                                            pipelineState,
                                                            std::move(uniformBufferBindings),
                                                            std::move(textureBindings));
            }

            [[nodiscard]] std::unique_ptr<RhiCommandList> CreateCommandList() override
            {
                ComPtr<ID3D12CommandAllocator> commandAllocator;
                HRESULT result =
                    device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateCommandAllocator", result));
                    return nullptr;
                }

                ComPtr<ID3D12GraphicsCommandList> commandList;
                result = device_->CreateCommandList(
                    0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

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
