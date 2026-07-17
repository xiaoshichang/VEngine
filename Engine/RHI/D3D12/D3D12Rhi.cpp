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
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <memory>
#include <mutex>
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
            case RhiPrimitiveTopology::LineList:
                return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            case RhiPrimitiveTopology::TriangleList:
            default:
                return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            }
        }

        D3D_PRIMITIVE_TOPOLOGY ToD3DTopology(RhiPrimitiveTopology topology)
        {
            switch (topology)
            {
            case RhiPrimitiveTopology::LineList:
                return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            case RhiPrimitiveTopology::TriangleList:
            default:
                return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            }
        }

        D3D12_FILTER ToD3D12Filter(RhiSamplerFilter filter)
        {
            switch (filter)
            {
            case RhiSamplerFilter::Point:
                return D3D12_FILTER_MIN_MAG_MIP_POINT;
            case RhiSamplerFilter::Trilinear:
                return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            case RhiSamplerFilter::Anisotropic:
                return D3D12_FILTER_ANISOTROPIC;
            case RhiSamplerFilter::Bilinear:
            default:
                return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            }
        }

        D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(RhiSamplerAddressMode mode)
        {
            switch (mode)
            {
            case RhiSamplerAddressMode::Mirror:
                return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
            case RhiSamplerAddressMode::Clamp:
                return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            case RhiSamplerAddressMode::Border:
                return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            case RhiSamplerAddressMode::Wrap:
            default:
                return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            }
        }

        D3D12_CULL_MODE ToD3D12CullMode(RhiCullMode cullMode)
        {
            switch (cullMode)
            {
            case RhiCullMode::None:
                return D3D12_CULL_MODE_NONE;
            case RhiCullMode::Front:
                return D3D12_CULL_MODE_FRONT;
            case RhiCullMode::Back:
            default:
                return D3D12_CULL_MODE_BACK;
            }
        }

        D3D12_COMPARISON_FUNC ToD3D12ComparisonFunc(RhiCompareFunction compareFunction)
        {
            switch (compareFunction)
            {
            case RhiCompareFunction::Never:
                return D3D12_COMPARISON_FUNC_NEVER;
            case RhiCompareFunction::Less:
                return D3D12_COMPARISON_FUNC_LESS;
            case RhiCompareFunction::Equal:
                return D3D12_COMPARISON_FUNC_EQUAL;
            case RhiCompareFunction::Always:
                return D3D12_COMPARISON_FUNC_ALWAYS;
            case RhiCompareFunction::LessEqual:
                return D3D12_COMPARISON_FUNC_LESS_EQUAL;
            case RhiCompareFunction::Greater:
                return D3D12_COMPARISON_FUNC_GREATER;
            case RhiCompareFunction::NotEqual:
                return D3D12_COMPARISON_FUNC_NOT_EQUAL;
            case RhiCompareFunction::GreaterEqual:
            default:
                return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            }
        }

        D3D12_BLEND ToD3D12Blend(RhiBlendFactor factor)
        {
            switch (factor)
            {
            case RhiBlendFactor::Zero:
                return D3D12_BLEND_ZERO;
            case RhiBlendFactor::One:
                return D3D12_BLEND_ONE;
            case RhiBlendFactor::SourceColor:
                return D3D12_BLEND_SRC_COLOR;
            case RhiBlendFactor::OneMinusSourceColor:
                return D3D12_BLEND_INV_SRC_COLOR;
            case RhiBlendFactor::SourceAlpha:
                return D3D12_BLEND_SRC_ALPHA;
            case RhiBlendFactor::OneMinusSourceAlpha:
                return D3D12_BLEND_INV_SRC_ALPHA;
            case RhiBlendFactor::DestinationColor:
                return D3D12_BLEND_DEST_COLOR;
            case RhiBlendFactor::OneMinusDestinationColor:
                return D3D12_BLEND_INV_DEST_COLOR;
            case RhiBlendFactor::DestinationAlpha:
                return D3D12_BLEND_DEST_ALPHA;
            case RhiBlendFactor::OneMinusDestinationAlpha:
            default:
                return D3D12_BLEND_INV_DEST_ALPHA;
            }
        }

        D3D12_BLEND_OP ToD3D12BlendOp(RhiBlendOperation operation)
        {
            switch (operation)
            {
            case RhiBlendOperation::Add:
                return D3D12_BLEND_OP_ADD;
            case RhiBlendOperation::Subtract:
                return D3D12_BLEND_OP_SUBTRACT;
            case RhiBlendOperation::ReverseSubtract:
                return D3D12_BLEND_OP_REV_SUBTRACT;
            case RhiBlendOperation::Min:
                return D3D12_BLEND_OP_MIN;
            case RhiBlendOperation::Max:
            default:
                return D3D12_BLEND_OP_MAX;
            }
        }

        D3D12_STENCIL_OP ToD3D12StencilOp(RhiStencilOperation operation)
        {
            switch (operation)
            {
            case RhiStencilOperation::Keep:
                return D3D12_STENCIL_OP_KEEP;
            case RhiStencilOperation::Zero:
                return D3D12_STENCIL_OP_ZERO;
            case RhiStencilOperation::Replace:
                return D3D12_STENCIL_OP_REPLACE;
            case RhiStencilOperation::IncrementClamp:
                return D3D12_STENCIL_OP_INCR_SAT;
            case RhiStencilOperation::DecrementClamp:
                return D3D12_STENCIL_OP_DECR_SAT;
            case RhiStencilOperation::Invert:
                return D3D12_STENCIL_OP_INVERT;
            case RhiStencilOperation::IncrementWrap:
                return D3D12_STENCIL_OP_INCR;
            case RhiStencilOperation::DecrementWrap:
            default:
                return D3D12_STENCIL_OP_DECR;
            }
        }

        D3D12_DEPTH_STENCILOP_DESC ToD3D12StencilFaceDesc(const RhiStencilFaceDesc& desc)
        {
            D3D12_DEPTH_STENCILOP_DESC d3dDesc = {};
            d3dDesc.StencilFailOp = ToD3D12StencilOp(desc.failOperation);
            d3dDesc.StencilDepthFailOp = ToD3D12StencilOp(desc.depthFailOperation);
            d3dDesc.StencilPassOp = ToD3D12StencilOp(desc.passOperation);
            d3dDesc.StencilFunc = ToD3D12ComparisonFunc(desc.compareFunction);
            return d3dDesc;
        }

        UINT8 ToD3D12ColorWriteMask(uint8_t mask)
        {
            UINT8 d3dMask = 0;
            if ((mask & RhiColorWriteRed) != 0)
            {
                d3dMask |= D3D12_COLOR_WRITE_ENABLE_RED;
            }
            if ((mask & RhiColorWriteGreen) != 0)
            {
                d3dMask |= D3D12_COLOR_WRITE_ENABLE_GREEN;
            }
            if ((mask & RhiColorWriteBlue) != 0)
            {
                d3dMask |= D3D12_COLOR_WRITE_ENABLE_BLUE;
            }
            if ((mask & RhiColorWriteAlpha) != 0)
            {
                d3dMask |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
            }
            return d3dMask;
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

        class D3D12ShaderResourceDescriptorAllocator final : public RhiNativeShaderResourceDescriptorAllocator
        {
        public:
            [[nodiscard]] bool Initialize(ID3D12Device* device, ID3D12Fence* fence)
            {
                if (device == nullptr || fence == nullptr)
                {
                    return false;
                }

                D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
                heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                heapDesc.NumDescriptors = DescriptorCapacity;
                heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

                if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap_))))
                {
                    return false;
                }

                fence_ = fence;
                descriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                cpuStart_ = heap_->GetCPUDescriptorHandleForHeapStart();
                gpuStart_ = heap_->GetGPUDescriptorHandleForHeapStart();
                allocatedSlots_.assign(DescriptorCapacity, false);
                freeIndices_.reserve(DescriptorCapacity);
                retiredIndices_.reserve(DescriptorCapacity);
                return descriptorSize_ != 0;
            }

            [[nodiscard]] void* GetNativeHeapHandle() const noexcept override
            {
                return heap_.Get();
            }

            [[nodiscard]] bool Allocate(RhiNativeShaderResourceDescriptor& outDescriptor) override
            {
                std::scoped_lock lock(mutex_);
                CollectCompletedSlots();

                uint32_t descriptorIndex = 0;
                if (!freeIndices_.empty())
                {
                    descriptorIndex = freeIndices_.back();
                    freeIndices_.pop_back();
                }
                else
                {
                    if (nextIndex_ >= DescriptorCapacity)
                    {
                        outDescriptor = {};
                        return false;
                    }

                    descriptorIndex = nextIndex_;
                    ++nextIndex_;
                }

                VE_ASSERT_MESSAGE(!allocatedSlots_[descriptorIndex], "D3D12 shader-resource descriptor slot is already allocated.");
                allocatedSlots_[descriptorIndex] = true;
                outDescriptor.cpuHandle = cpuStart_.ptr + (static_cast<uint64_t>(descriptorIndex) * descriptorSize_);
                outDescriptor.gpuHandle = gpuStart_.ptr + (static_cast<uint64_t>(descriptorIndex) * descriptorSize_);
                return true;
            }

            void Release(RhiNativeShaderResourceDescriptor descriptor) noexcept override
            {
                std::scoped_lock lock(mutex_);

                const bool cpuHandleInRange = descriptor.cpuHandle >= cpuStart_.ptr;
                const bool gpuHandleInRange = descriptor.gpuHandle >= gpuStart_.ptr;
                const uint64_t cpuOffset = cpuHandleInRange ? descriptor.cpuHandle - cpuStart_.ptr : 0;
                const uint64_t gpuOffset = gpuHandleInRange ? descriptor.gpuHandle - gpuStart_.ptr : 0;
                const bool aligned = descriptorSize_ != 0 && cpuOffset % descriptorSize_ == 0 && gpuOffset % descriptorSize_ == 0;
                const uint64_t cpuIndex = descriptorSize_ != 0 ? cpuOffset / descriptorSize_ : DescriptorCapacity;
                const uint64_t gpuIndex = descriptorSize_ != 0 ? gpuOffset / descriptorSize_ : DescriptorCapacity;
                const bool valid = cpuHandleInRange && gpuHandleInRange && aligned && cpuIndex == gpuIndex && cpuIndex < nextIndex_ &&
                                   allocatedSlots_[static_cast<size_t>(cpuIndex)];
                VE_ASSERT_MESSAGE(valid, "D3D12 shader-resource descriptor release is invalid or duplicated.");
                if (!valid)
                {
                    return;
                }

                const uint32_t descriptorIndex = static_cast<uint32_t>(cpuIndex);
                allocatedSlots_[descriptorIndex] = false;
                retiredIndices_.push_back(RetiredDescriptor{descriptorIndex, lastSubmittedFenceValue_.load(std::memory_order_acquire)});
            }

            void NotifySubmission(uint64_t fenceValue) noexcept
            {
                lastSubmittedFenceValue_.store(fenceValue, std::memory_order_release);
            }

        private:
            struct RetiredDescriptor
            {
                uint32_t index = 0;
                uint64_t fenceValue = 0;
            };

            void CollectCompletedSlots()
            {
                const uint64_t completedFenceValue = fence_->GetCompletedValue();
                auto removeBegin = std::remove_if(retiredIndices_.begin(),
                                                  retiredIndices_.end(),
                                                  [this, completedFenceValue](const RetiredDescriptor& retiredDescriptor)
                                                  {
                                                      if (retiredDescriptor.fenceValue > completedFenceValue)
                                                      {
                                                          return false;
                                                      }

                                                      freeIndices_.push_back(retiredDescriptor.index);
                                                      return true;
                                                  });
                retiredIndices_.erase(removeBegin, retiredIndices_.end());
            }

            static constexpr uint32_t DescriptorCapacity = 4096;

            ComPtr<ID3D12DescriptorHeap> heap_;
            ComPtr<ID3D12Fence> fence_;
            D3D12_CPU_DESCRIPTOR_HANDLE cpuStart_ = {};
            D3D12_GPU_DESCRIPTOR_HANDLE gpuStart_ = {};
            uint32_t descriptorSize_ = 0;
            uint32_t nextIndex_ = 0;
            std::vector<bool> allocatedSlots_;
            std::vector<uint32_t> freeIndices_;
            std::vector<RetiredDescriptor> retiredIndices_;
            std::atomic<uint64_t> lastSubmittedFenceValue_{0};
            std::mutex mutex_;
        };

        class D3D12Buffer final : public RhiBuffer
        {
        public:
            D3D12Buffer(ComPtr<ID3D12Resource> resource, uint64_t size, RhiBufferMemoryUsage memoryUsage, std::byte* mappedData)
                : resource_(std::move(resource))
                , size_(size)
                , memoryUsage_(memoryUsage)
                , mappedData_(mappedData)
            {
            }

            ~D3D12Buffer() override
            {
                if (mappedData_ != nullptr)
                {
                    resource_->Unmap(0, nullptr);
                }
            }

            [[nodiscard]] uint64_t GetSize() const noexcept override
            {
                return size_;
            }

            [[nodiscard]] ID3D12Resource* GetNativeResource() const noexcept
            {
                return resource_.Get();
            }

            [[nodiscard]] RhiBufferMemoryUsage GetMemoryUsage() const noexcept
            {
                return memoryUsage_;
            }

            [[nodiscard]] std::byte* GetMappedData() const noexcept
            {
                return mappedData_;
            }

        private:
            ComPtr<ID3D12Resource> resource_;
            uint64_t size_ = 0;
            RhiBufferMemoryUsage memoryUsage_ = RhiBufferMemoryUsage::GpuOnly;
            std::byte* mappedData_ = nullptr;
        };

        class D3D12Texture final : public RhiTexture
        {
        public:
            D3D12Texture(ComPtr<ID3D12Resource> resource,
                         ComPtr<ID3D12DescriptorHeap> rtvHeap,
                         ComPtr<ID3D12DescriptorHeap> dsvHeap,
                         std::shared_ptr<D3D12ShaderResourceDescriptorAllocator> shaderResourceDescriptorAllocator,
                         RhiNativeShaderResourceDescriptor shaderResourceDescriptor,
                         RhiTextureDesc desc,
                         D3D12_RESOURCE_STATES resourceState,
                         UINT dsvDescriptorSize)
                : resource_(std::move(resource))
                , rtvHeap_(std::move(rtvHeap))
                , dsvHeap_(std::move(dsvHeap))
                , shaderResourceDescriptorAllocator_(std::move(shaderResourceDescriptorAllocator))
                , shaderResourceDescriptor_(shaderResourceDescriptor)
                , desc_(desc)
                , resourceState_(resourceState)
                , dsvDescriptorSize_(dsvDescriptorSize)
            {
            }

            ~D3D12Texture() override
            {
                if (HasShaderResourceView())
                {
                    shaderResourceDescriptorAllocator_->Release(shaderResourceDescriptor_);
                }
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

            [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView(bool readOnly) const noexcept
            {
                VE_ASSERT(dsvHeap_ != nullptr);
                D3D12_CPU_DESCRIPTOR_HANDLE handle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
                if (readOnly)
                {
                    handle.ptr += dsvDescriptorSize_;
                }
                return handle;
            }

            [[nodiscard]] bool HasShaderResourceView() const noexcept
            {
                return shaderResourceDescriptorAllocator_ != nullptr && shaderResourceDescriptor_.gpuHandle != 0;
            }

            [[nodiscard]] ID3D12DescriptorHeap* GetShaderResourceViewHeap() const noexcept
            {
                VE_ASSERT(HasShaderResourceView());
                return static_cast<ID3D12DescriptorHeap*>(shaderResourceDescriptorAllocator_->GetNativeHeapHandle());
            }

            [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceView() const noexcept
            {
                VE_ASSERT(HasShaderResourceView());
                return D3D12_GPU_DESCRIPTOR_HANDLE{shaderResourceDescriptor_.gpuHandle};
            }

            [[nodiscard]] void* GetNativeSampledViewHandle() const noexcept override
            {
                return reinterpret_cast<void*>(static_cast<uintptr_t>(shaderResourceDescriptor_.gpuHandle));
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
            std::shared_ptr<D3D12ShaderResourceDescriptorAllocator> shaderResourceDescriptorAllocator_;
            RhiNativeShaderResourceDescriptor shaderResourceDescriptor_ = {};
            RhiTextureDesc desc_ = {};
            D3D12_RESOURCE_STATES resourceState_ = D3D12_RESOURCE_STATE_COMMON;
            UINT dsvDescriptorSize_ = 0;
        };

        class D3D12Sampler final : public RhiSampler
        {
        public:
            D3D12Sampler(ComPtr<ID3D12DescriptorHeap> samplerHeap, RhiSamplerDesc desc)
                : samplerHeap_(std::move(samplerHeap))
                , desc_(desc)
            {
            }

            [[nodiscard]] RhiSamplerFilter GetFilter() const noexcept override
            {
                return desc_.filter;
            }

            [[nodiscard]] ID3D12DescriptorHeap* GetSamplerHeap() const noexcept
            {
                return samplerHeap_.Get();
            }

            [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetSampler() const noexcept
            {
                VE_ASSERT(samplerHeap_ != nullptr);
                return samplerHeap_->GetGPUDescriptorHandleForHeapStart();
            }

        private:
            ComPtr<ID3D12DescriptorHeap> samplerHeap_;
            RhiSamplerDesc desc_ = {};
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

            [[nodiscard]] uint32_t GetBufferCount() const noexcept override
            {
                return bufferCount_;
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

            [[nodiscard]] bool BeginRenderPass(RhiSwapchain& swapchain, const RhiRenderPassBeginInfo& beginInfo) override
            {
                auto* d3dSwapchain = dynamic_cast<D3D12Swapchain*>(&swapchain);

                if (d3dSwapchain == nullptr)
                {
                    return false;
                }

                const RhiRenderPassColorAttachmentInfo& colorAttachment = beginInfo.colorAttachment;
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

                if (beginInfo.hasDepthAttachment)
                {
                    auto* d3dDepthTexture = dynamic_cast<D3D12Texture*>(beginInfo.depthAttachment.texture);
                    if (d3dDepthTexture == nullptr || !d3dDepthTexture->HasDepthStencilView())
                    {
                        return false;
                    }

                    activeDepthTexture_ = d3dDepthTexture;
                    dsvHandle = d3dDepthTexture->GetDepthStencilView(beginInfo.depthAttachment.readOnly);
                    const D3D12_RESOURCE_STATES depthState =
                        beginInfo.depthAttachment.readOnly ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE;
                    TransitionResource(d3dDepthTexture->GetNativeResource(), d3dDepthTexture->GetResourceState(), depthState);
                    d3dDepthTexture->SetResourceState(depthState);
                }

                commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, beginInfo.hasDepthAttachment ? &dsvHandle : nullptr);

                if (colorAttachment.loadAction == RhiLoadAction::Clear)
                {
                    const float clearColor[4] = {
                        colorAttachment.clearColor.r, colorAttachment.clearColor.g, colorAttachment.clearColor.b, colorAttachment.clearColor.a};
                    commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
                }

                if (beginInfo.hasDepthAttachment && beginInfo.depthAttachment.loadAction == RhiLoadAction::Clear)
                {
                    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, beginInfo.depthAttachment.clearDepth, 0, 0, nullptr);
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

                if (activeTexture_ != nullptr && activeTexture_->HasShaderResourceView())
                {
                    TransitionResource(
                        activeTexture_->GetNativeResource(), activeTexture_->GetResourceState(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    activeTexture_->SetResourceState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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

            void SetUniformBuffer(RhiShaderStage stage, uint32_t slot, const RhiBuffer& buffer, uint64_t offset, uint64_t size) override
            {
                VE_ASSERT(size > 0);
                VE_ASSERT(offset % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT == 0);
                VE_ASSERT(offset + size <= buffer.GetSize());
                const auto& d3dBuffer = static_cast<const D3D12Buffer&>(buffer);
                const UINT rootParameterIndex = ResolveUniformRootParameter(stage, slot);
                commandList_->SetGraphicsRootConstantBufferView(rootParameterIndex, d3dBuffer.GetNativeResource()->GetGPUVirtualAddress() + offset);
            }

            void SetTexture(RhiShaderStage stage, uint32_t slot, const RhiTexture& texture) override
            {
                VE_ASSERT(stage == RhiShaderStage::Fragment && slot == 0);
                const auto& d3dTexture = static_cast<const D3D12Texture&>(texture);
                VE_ASSERT(d3dTexture.HasShaderResourceView());
                activeResourceHeap_ = d3dTexture.GetShaderResourceViewHeap();
                ApplyDescriptorHeaps();
                commandList_->SetGraphicsRootDescriptorTable(4, d3dTexture.GetShaderResourceView());
            }

            void SetSampler(RhiShaderStage stage, uint32_t slot, const RhiSampler& sampler) override
            {
                VE_ASSERT(stage == RhiShaderStage::Fragment && slot == 0);
                const auto& d3dSampler = static_cast<const D3D12Sampler&>(sampler);
                activeSamplerHeap_ = d3dSampler.GetSamplerHeap();
                ApplyDescriptorHeaps();
                commandList_->SetGraphicsRootDescriptorTable(5, d3dSampler.GetSampler());
            }

            void Draw(uint32_t vertexCount, uint32_t firstVertex) override
            {
                commandList_->DrawInstanced(vertexCount, 1, firstVertex, 0);
            }

            void DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) override
            {
                commandList_->DrawIndexedInstanced(indexCount, 1, firstIndex, vertexOffset, 0);
            }

            [[nodiscard]] void* GetNativeCommandBufferHandle() const noexcept override
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
                static_cast<void>(stage);
                VE_ASSERT(slot < 4);
                return slot;
            }

            void ApplyDescriptorHeaps()
            {
                ID3D12DescriptorHeap* heaps[2] = {};
                UINT heapCount = 0;
                if (activeResourceHeap_ != nullptr)
                {
                    heaps[heapCount++] = activeResourceHeap_;
                }
                if (activeSamplerHeap_ != nullptr)
                {
                    heaps[heapCount++] = activeSamplerHeap_;
                }

                if (heapCount > 0)
                {
                    commandList_->SetDescriptorHeaps(heapCount, heaps);
                }
            }

            ComPtr<ID3D12Device> device_;
            ComPtr<ID3D12CommandAllocator> commandAllocator_;
            ComPtr<ID3D12GraphicsCommandList> commandList_;
            D3D12Swapchain* activeSwapchain_ = nullptr;
            D3D12Texture* activeTexture_ = nullptr;
            D3D12Texture* activeDepthTexture_ = nullptr;
            ID3D12DescriptorHeap* activeResourceHeap_ = nullptr;
            ID3D12DescriptorHeap* activeSamplerHeap_ = nullptr;
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

                shaderResourceDescriptorAllocator_ = std::make_shared<D3D12ShaderResourceDescriptorAllocator>();
                if (!shaderResourceDescriptorAllocator_->Initialize(device_.Get(), fence_.Get()))
                {
                    SetLastError("Failed to create the D3D12 shared shader-resource descriptor heap.");
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

            [[nodiscard]] RhiNativeShaderResourceDescriptorAllocator* GetNativeShaderResourceDescriptorAllocator() const noexcept override
            {
                return shaderResourceDescriptorAllocator_.get();
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

                std::byte* persistentMappedData = nullptr;
                if (desc.memoryUsage == RhiBufferMemoryUsage::CpuToGpu)
                {
                    void* mappedData = nullptr;
                    D3D12_RANGE readRange = {};
                    result = resource->Map(0, &readRange, &mappedData);

                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Resource::Map", result));
                        return nullptr;
                    }

                    persistentMappedData = static_cast<std::byte*>(mappedData);
                    if (desc.initialData != nullptr && desc.size > 0)
                    {
                        std::memcpy(persistentMappedData, desc.initialData, static_cast<size_t>(desc.size));
                    }
                }
                else if (desc.initialData != nullptr && desc.size > 0)
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

                return std::make_unique<D3D12Buffer>(resource, desc.size, desc.memoryUsage, persistentMappedData);
            }

            void UpdateBuffer(RhiBuffer& buffer, uint64_t offset, const void* data, uint64_t size, RhiBufferUpdateMode updateMode) override
            {
                static_cast<void>(updateMode);
                auto& d3dBuffer = static_cast<D3D12Buffer&>(buffer);
                VE_ASSERT(d3dBuffer.GetMemoryUsage() == RhiBufferMemoryUsage::CpuToGpu);
                VE_ASSERT(d3dBuffer.GetMappedData() != nullptr);
                VE_ASSERT(data != nullptr);
                VE_ASSERT(size > 0);
                VE_ASSERT(offset + size <= d3dBuffer.GetSize());
                std::memcpy(d3dBuffer.GetMappedData() + offset, data, static_cast<size_t>(size));
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

                D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;
                if (desc.initialData != nullptr && desc.initialDataSize > 0)
                {
                    if (desc.mipLevelCount != 1 || desc.initialDataRowPitch == 0)
                    {
                        SetLastError("D3D12 texture initial upload requires one mip level and a row pitch.");
                        return nullptr;
                    }

                    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
                    UINT rowCount = 0;
                    UINT64 rowSizeInBytes = 0;
                    UINT64 uploadBufferSize = 0;
                    device_->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &footprint, &rowCount, &rowSizeInBytes, &uploadBufferSize);

                    D3D12_HEAP_PROPERTIES uploadHeapProperties = {};
                    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
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

                    ComPtr<ID3D12Resource> uploadBuffer;
                    result = device_->CreateCommittedResource(&uploadHeapProperties,
                                                              D3D12_HEAP_FLAG_NONE,
                                                              &uploadResourceDesc,
                                                              D3D12_RESOURCE_STATE_GENERIC_READ,
                                                              nullptr,
                                                              IID_PPV_ARGS(&uploadBuffer));
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Device::CreateCommittedResource texture upload", result));
                        return nullptr;
                    }

                    void* mappedData = nullptr;
                    D3D12_RANGE readRange = {};
                    result = uploadBuffer->Map(0, &readRange, &mappedData);
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Resource::Map texture upload", result));
                        return nullptr;
                    }

                    const auto* sourceBytes = static_cast<const std::byte*>(desc.initialData);
                    auto* destinationBytes = static_cast<std::byte*>(mappedData) + footprint.Offset;
                    const uint64_t sourceRowPitch = desc.initialDataRowPitch;
                    const uint64_t copyRowBytes = (std::min)(rowSizeInBytes, sourceRowPitch);
                    for (UINT row = 0; row < rowCount; ++row)
                    {
                        std::memcpy(destinationBytes + (static_cast<uint64_t>(row) * footprint.Footprint.RowPitch),
                                    sourceBytes + (static_cast<uint64_t>(row) * sourceRowPitch),
                                    static_cast<size_t>(copyRowBytes));
                    }
                    uploadBuffer->Unmap(0, nullptr);

                    ComPtr<ID3D12CommandAllocator> uploadAllocator;
                    result = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator));
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Device::CreateCommandAllocator texture upload", result));
                        return nullptr;
                    }

                    ComPtr<ID3D12GraphicsCommandList> uploadCommandList;
                    result = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAllocator.Get(), nullptr, IID_PPV_ARGS(&uploadCommandList));
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Device::CreateCommandList texture upload", result));
                        return nullptr;
                    }

                    D3D12_RESOURCE_BARRIER copyBarrier = {};
                    copyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    copyBarrier.Transition.pResource = resource.Get();
                    copyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    copyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
                    copyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                    uploadCommandList->ResourceBarrier(1, &copyBarrier);

                    D3D12_TEXTURE_COPY_LOCATION destinationLocation = {};
                    destinationLocation.pResource = resource.Get();
                    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    destinationLocation.SubresourceIndex = 0;

                    D3D12_TEXTURE_COPY_LOCATION sourceLocation = {};
                    sourceLocation.pResource = uploadBuffer.Get();
                    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    sourceLocation.PlacedFootprint = footprint;

                    uploadCommandList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);

                    D3D12_RESOURCE_BARRIER shaderResourceBarrier = {};
                    shaderResourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    shaderResourceBarrier.Transition.pResource = resource.Get();
                    shaderResourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    shaderResourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                    shaderResourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    uploadCommandList->ResourceBarrier(1, &shaderResourceBarrier);

                    result = uploadCommandList->Close();
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12GraphicsCommandList::Close texture upload", result));
                        return nullptr;
                    }

                    ID3D12CommandList* nativeCommandLists[] = {uploadCommandList.Get()};
                    queue_->ExecuteCommandLists(1, nativeCommandLists);
                    if (!SignalAndWait())
                    {
                        return nullptr;
                    }

                    resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
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
                UINT dsvDescriptorSize = 0;
                if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::DepthStencil)) != 0)
                {
                    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
                    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
                    heapDesc.NumDescriptors = 2;
                    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

                    result = device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsvHeap));
                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Device::CreateDescriptorHeap texture DSV", result));
                        return nullptr;
                    }

                    dsvDescriptorSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
                    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
                    dsvDesc.Format = ToDxgiFormat(desc.format);
                    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

                    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
                    device_->CreateDepthStencilView(resource.Get(), &dsvDesc, dsvHandle);
                    dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
                    dsvHandle.ptr += dsvDescriptorSize;
                    device_->CreateDepthStencilView(resource.Get(), &dsvDesc, dsvHandle);
                }

                RhiNativeShaderResourceDescriptor shaderResourceDescriptor = {};
                if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::Sampled)) != 0)
                {
                    if (!shaderResourceDescriptorAllocator_->Allocate(shaderResourceDescriptor))
                    {
                        SetLastError("D3D12 shader-resource descriptor heap is exhausted.");
                        return nullptr;
                    }

                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                    srvDesc.Format = ToDxgiFormat(desc.format);
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srvDesc.Texture2D.MipLevels = desc.mipLevelCount;
                    device_->CreateShaderResourceView(resource.Get(), &srvDesc, D3D12_CPU_DESCRIPTOR_HANDLE{shaderResourceDescriptor.cpuHandle});
                }

                return std::make_unique<D3D12Texture>(
                    resource, rtvHeap, dsvHeap, shaderResourceDescriptorAllocator_, shaderResourceDescriptor, desc, resourceState, dsvDescriptorSize);
            }

            [[nodiscard]] std::unique_ptr<RhiSampler> CreateSampler(const RhiSamplerDesc& desc) override
            {
                D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
                heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
                heapDesc.NumDescriptors = 1;
                heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

                ComPtr<ID3D12DescriptorHeap> samplerHeap;
                HRESULT result = device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&samplerHeap));
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateDescriptorHeap sampler", result));
                    return nullptr;
                }

                D3D12_SAMPLER_DESC samplerDesc = {};
                samplerDesc.Filter = ToD3D12Filter(desc.filter);
                samplerDesc.AddressU = ToD3D12AddressMode(desc.addressU);
                samplerDesc.AddressV = ToD3D12AddressMode(desc.addressV);
                samplerDesc.AddressW = ToD3D12AddressMode(desc.addressW);
                samplerDesc.MipLODBias = desc.mipBias;
                samplerDesc.MaxAnisotropy = desc.maxAnisotropy;
                samplerDesc.ComparisonFunc = ToD3D12ComparisonFunc(desc.comparisonFunction);
                samplerDesc.BorderColor[0] = desc.borderColor.r;
                samplerDesc.BorderColor[1] = desc.borderColor.g;
                samplerDesc.BorderColor[2] = desc.borderColor.b;
                samplerDesc.BorderColor[3] = desc.borderColor.a;
                samplerDesc.MinLOD = desc.minLod;
                samplerDesc.MaxLOD = desc.maxLod;
                device_->CreateSampler(&samplerDesc, samplerHeap->GetCPUDescriptorHandleForHeapStart());

                return std::make_unique<D3D12Sampler>(samplerHeap, desc);
            }

            [[nodiscard]] std::unique_ptr<RhiShaderModule> CreateShaderModule(const RhiShaderModuleDesc& desc) override
            {
                if (desc.codeFormat == RhiShaderCodeFormat::Bytecode)
                {
                    if (desc.bytecode == nullptr || desc.bytecodeSize == 0)
                    {
                        SetLastError("D3D12 shader module bytecode input is empty.");
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
                    return std::make_unique<D3D12ShaderModule>(desc.stage, bytecode);
                }

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
                const RhiBoundShaderStateDesc& boundShaderState = desc.boundShaderState;
                const auto* vertexShaderModule = dynamic_cast<const D3D12ShaderModule*>(boundShaderState.vertexShader);
                const auto* fragmentShaderModule = dynamic_cast<const D3D12ShaderModule*>(boundShaderState.fragmentShader);

                if (vertexShaderModule == nullptr || fragmentShaderModule == nullptr)
                {
                    SetLastError("D3D12 graphics pipeline requires D3D12 shader modules.");
                    return nullptr;
                }

                D3D12_DESCRIPTOR_RANGE descriptorRanges[2] = {};
                descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                descriptorRanges[0].NumDescriptors = 1;
                descriptorRanges[0].BaseShaderRegister = 0;
                descriptorRanges[0].RegisterSpace = 0;
                descriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                descriptorRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                descriptorRanges[1].NumDescriptors = 1;
                descriptorRanges[1].BaseShaderRegister = 0;
                descriptorRanges[1].RegisterSpace = 0;
                descriptorRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

                D3D12_ROOT_PARAMETER rootParameters[6] = {};
                for (UINT slot = 0; slot < 4; ++slot)
                {
                    rootParameters[slot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                    rootParameters[slot].Descriptor.ShaderRegister = slot;
                    rootParameters[slot].Descriptor.RegisterSpace = 0;
                    rootParameters[slot].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                }
                rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
                rootParameters[4].DescriptorTable.pDescriptorRanges = &descriptorRanges[0];
                rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
                rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                rootParameters[5].DescriptorTable.NumDescriptorRanges = 1;
                rootParameters[5].DescriptorTable.pDescriptorRanges = &descriptorRanges[1];
                rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

                D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
                rootSignatureDesc.NumParameters = 6;
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
                inputElements.reserve(boundShaderState.vertexDeclaration.attributeCount);

                for (uint32_t index = 0; index < boundShaderState.vertexDeclaration.attributeCount; ++index)
                {
                    const RhiVertexAttributeDesc& attribute = boundShaderState.vertexDeclaration.attributes[index];
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
                rasterizerDesc.FillMode = desc.rasterizerState.fillMode == RhiFillMode::Wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
                rasterizerDesc.CullMode = ToD3D12CullMode(desc.rasterizerState.cullMode);
                rasterizerDesc.FrontCounterClockwise = desc.rasterizerState.frontCounterClockwise ? TRUE : FALSE;
                rasterizerDesc.DepthBias = desc.rasterizerState.depthBias;
                rasterizerDesc.DepthBiasClamp = desc.rasterizerState.depthBiasClamp;
                rasterizerDesc.SlopeScaledDepthBias = desc.rasterizerState.slopeScaledDepthBias;
                rasterizerDesc.DepthClipEnable = desc.rasterizerState.depthClipEnabled ? TRUE : FALSE;
                rasterizerDesc.MultisampleEnable = desc.rasterizerState.multisampleEnabled ? TRUE : FALSE;
                rasterizerDesc.AntialiasedLineEnable = desc.rasterizerState.antialiasedLineEnabled ? TRUE : FALSE;
                rasterizerDesc.ForcedSampleCount = 0;
                rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

                D3D12_BLEND_DESC blendDesc = {};
                blendDesc.AlphaToCoverageEnable = desc.blendState.alphaToCoverageEnabled ? TRUE : FALSE;
                blendDesc.IndependentBlendEnable = desc.blendState.independentBlendEnabled ? TRUE : FALSE;
                for (uint32_t index = 0; index < RhiMaxColorAttachments; ++index)
                {
                    const RhiBlendRenderTargetDesc& targetDesc = desc.blendState.renderTargets[index];
                    blendDesc.RenderTarget[index].BlendEnable = targetDesc.blendEnabled ? TRUE : FALSE;
                    blendDesc.RenderTarget[index].SrcBlend = ToD3D12Blend(targetDesc.sourceColorBlendFactor);
                    blendDesc.RenderTarget[index].DestBlend = ToD3D12Blend(targetDesc.destinationColorBlendFactor);
                    blendDesc.RenderTarget[index].BlendOp = ToD3D12BlendOp(targetDesc.colorBlendOperation);
                    blendDesc.RenderTarget[index].SrcBlendAlpha = ToD3D12Blend(targetDesc.sourceAlphaBlendFactor);
                    blendDesc.RenderTarget[index].DestBlendAlpha = ToD3D12Blend(targetDesc.destinationAlphaBlendFactor);
                    blendDesc.RenderTarget[index].BlendOpAlpha = ToD3D12BlendOp(targetDesc.alphaBlendOperation);
                    blendDesc.RenderTarget[index].RenderTargetWriteMask = ToD3D12ColorWriteMask(targetDesc.colorWriteMask);
                }

                D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
                depthStencilDesc.DepthEnable = desc.depthStencilState.depthTestEnabled ? TRUE : FALSE;
                depthStencilDesc.DepthWriteMask = desc.depthStencilState.depthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
                depthStencilDesc.DepthFunc = ToD3D12ComparisonFunc(desc.depthStencilState.depthCompareFunction);
                depthStencilDesc.StencilEnable = desc.depthStencilState.stencilEnabled ? TRUE : FALSE;
                depthStencilDesc.StencilReadMask = desc.depthStencilState.stencilReadMask;
                depthStencilDesc.StencilWriteMask = desc.depthStencilState.stencilWriteMask;
                depthStencilDesc.FrontFace = ToD3D12StencilFaceDesc(desc.depthStencilState.frontFace);
                depthStencilDesc.BackFace = ToD3D12StencilFaceDesc(desc.depthStencilState.backFace);

                D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = {};
                pipelineDesc.pRootSignature = rootSignature.Get();
                pipelineDesc.VS = vertexShaderModule->GetBytecode();
                pipelineDesc.PS = fragmentShaderModule->GetBytecode();
                pipelineDesc.BlendState = blendDesc;
                pipelineDesc.SampleMask = UINT_MAX;
                pipelineDesc.RasterizerState = rasterizerDesc;
                pipelineDesc.DepthStencilState = depthStencilDesc;
                pipelineDesc.InputLayout = {inputElements.data(), static_cast<UINT>(inputElements.size())};
                pipelineDesc.PrimitiveTopologyType = ToD3D12TopologyType(desc.primitiveType);
                pipelineDesc.NumRenderTargets = 1;
                pipelineDesc.RTVFormats[0] = ToDxgiFormat(desc.colorFormat);
                pipelineDesc.DSVFormat = desc.depthStencilState.depthTestEnabled ? ToDxgiFormat(desc.depthFormat) : DXGI_FORMAT_UNKNOWN;
                pipelineDesc.SampleDesc.Count = 1;

                ComPtr<ID3D12PipelineState> pipelineState;
                result = device_->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&pipelineState));

                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12Device::CreateGraphicsPipelineState", result));
                    return nullptr;
                }

                return std::make_unique<D3D12PipelineState>(desc.primitiveType, rootSignature, pipelineState);
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

            [[nodiscard]] bool Submit(RhiCommandList& commandList, RhiFence* completionFence, uint64_t completionValue) override
            {
                auto* d3dCommandList = dynamic_cast<D3D12CommandList*>(&commandList);

                if (d3dCommandList == nullptr)
                {
                    SetLastError("D3D12 device can only submit D3D12 command lists.");
                    return false;
                }

                auto* nativeCommandList = static_cast<ID3D12GraphicsCommandList*>(d3dCommandList->GetNativeCommandBufferHandle());
                ID3D12CommandList* nativeCommandLists[] = {nativeCommandList};
                queue_->ExecuteCommandLists(1, nativeCommandLists);

                uint64_t submissionFenceValue = 0;
                if (!SignalInternalFence(submissionFenceValue))
                {
                    return false;
                }

                if (completionFence == nullptr)
                {
                    return true;
                }

                auto* d3dFence = dynamic_cast<D3D12FenceObject*>(completionFence);
                if (d3dFence == nullptr)
                {
                    SetLastError("D3D12 device can only signal D3D12 fences.");
                    return false;
                }

                const HRESULT result = queue_->Signal(d3dFence->GetNativeFence(), completionValue);
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12CommandQueue::Signal completion fence", result));
                    return false;
                }

                return true;
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
                uint64_t fenceValue = 0;
                if (!SignalInternalFence(fenceValue))
                {
                    return false;
                }

                if (fence_->GetCompletedValue() < fenceValue)
                {
                    const HRESULT result = fence_->SetEventOnCompletion(fenceValue, fenceEvent_);

                    if (FAILED(result))
                    {
                        SetLastError(MakeHResultError("ID3D12Fence::SetEventOnCompletion", result));
                        return false;
                    }

                    WaitForSingleObject(fenceEvent_, INFINITE);
                }

                return true;
            }

            [[nodiscard]] bool SignalInternalFence(uint64_t& outFenceValue)
            {
                outFenceValue = nextFenceValue_;
                ++nextFenceValue_;

                const HRESULT result = queue_->Signal(fence_.Get(), outFenceValue);
                if (FAILED(result))
                {
                    SetLastError(MakeHResultError("ID3D12CommandQueue::Signal", result));
                    return false;
                }

                shaderResourceDescriptorAllocator_->NotifySubmission(outFenceValue);
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
            std::shared_ptr<D3D12ShaderResourceDescriptorAllocator> shaderResourceDescriptorAllocator_;
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
