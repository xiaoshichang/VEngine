#pragma once

#include "Engine/RHI/Common/RhiTypes.h"

#include <memory>

namespace ve::rhi
{
    /// Base class for all RHI-owned objects.
    class RhiObject
    {
    public:
        RhiObject() = default;
        virtual ~RhiObject() = default;

        RhiObject(const RhiObject&) = delete;
        RhiObject& operator=(const RhiObject&) = delete;
        RhiObject(RhiObject&&) = delete;
        RhiObject& operator=(RhiObject&&) = delete;
    };

    /// Owns a backend-specific vertex, index, uniform, or staging buffer.
    class RhiBuffer : public RhiObject
    {
    public:
        /// Returns the size of this buffer in bytes.
        [[nodiscard]] virtual uint64_t GetSize() const noexcept = 0;
    };

    /// Owns a backend texture resource.
    class RhiTexture : public RhiObject
    {
    public:
        /// Returns the texture dimensionality.
        [[nodiscard]] virtual RhiTextureDimension GetDimension() const noexcept = 0;

        /// Returns the texture width in pixels.
        [[nodiscard]] virtual uint32_t GetWidth() const noexcept = 0;

        /// Returns the texture height in pixels.
        [[nodiscard]] virtual uint32_t GetHeight() const noexcept = 0;

        /// Returns the texture format.
        [[nodiscard]] virtual RhiFormat GetFormat() const noexcept = 0;

        /// Returns a backend-native sampled view handle when the backend exposes one.
        ///
        /// D3D11 returns ID3D11ShaderResourceView*. D3D12 returns the GPU descriptor handle encoded as the opaque
        /// pointer value. Other backends may return their native sampled texture handle or null when unsupported.
        [[nodiscard]] virtual void* GetNativeSampledViewHandle() const noexcept
        {
            return nullptr;
        }
    };

    /// Owns a backend sampler state used with sampled textures.
    class RhiSampler : public RhiObject
    {
    public:
        /// Returns the filter mode used by this sampler.
        [[nodiscard]] virtual RhiSamplerFilter GetFilter() const noexcept = 0;
    };

    /// Represents a graphics queue synchronization point.
    class RhiFence : public RhiObject
    {
    public:
        /// Returns true when the fence has reached or passed the requested value.
        [[nodiscard]] virtual bool IsComplete(uint64_t value) const noexcept = 0;

        /// Blocks until the fence reaches or passes the requested value.
        [[nodiscard]] virtual bool Wait(uint64_t value) = 0;

        /// Returns the latest value known to be completed by the backend.
        [[nodiscard]] virtual uint64_t GetCompletedValue() const noexcept = 0;
    };

    /// Owns a compiled backend shader object.
    class RhiShaderModule : public RhiObject
    {
    public:
        /// Returns the shader stage this module was compiled for.
        [[nodiscard]] virtual RhiShaderStage GetStage() const noexcept = 0;
    };

    /// Owns immutable graphics pipeline state for one render-pass shape.
    class RhiPipelineState : public RhiObject
    {
    public:
        /// Returns the primitive topology used by this pipeline.
        [[nodiscard]] virtual RhiPrimitiveTopology GetTopology() const noexcept = 0;
    };

    /// Owns a presentation surface and its back buffers.
    class RhiSwapchain : public RhiObject
    {
    public:
        /// Returns the current drawable/back-buffer size.
        [[nodiscard]] virtual RhiExtent2D GetExtent() const noexcept = 0;

        /// Returns the color format used by the swapchain back buffers.
        [[nodiscard]] virtual RhiFormat GetColorFormat() const noexcept = 0;

        /// Returns the number of back buffers/drawables used by the swapchain.
        [[nodiscard]] virtual uint32_t GetBufferCount() const noexcept = 0;

        /// Recreates the presentation buffers for a new non-zero drawable size.
        [[nodiscard]] virtual bool Resize(RhiExtent2D extent) = 0;

        /// Presents the current back buffer to the screen.
        [[nodiscard]] virtual bool Present() = 0;
    };

    /// Records rendering commands using the common RHI command-list shape.
    class RhiCommandList : public RhiObject
    {
    public:
        /// Begins recording or preparing a command list for submission.
        [[nodiscard]] virtual bool Begin() = 0;

        /// Finishes command recording.
        [[nodiscard]] virtual bool End() = 0;

        /// Begins rendering with physical attachments resolved by the caller. A null color texture selects the
        /// swapchain's current back buffer only when both color attachment flags are set.
        [[nodiscard]] virtual bool BeginRenderPass(RhiSwapchain& swapchain, const RhiRenderPassBeginInfo& beginInfo) = 0;

        /// Ends the active render pass.
        virtual void EndRenderPass() = 0;

        /// Copies a texture into the current swapchain back buffer.
        ///
        /// The source texture must match the swapchain color format and extent in the first implementation. Backends
        /// may later replace this with a scaling/color-conversion blit path while keeping the frame-pipeline contract.
        [[nodiscard]] virtual bool CopyTextureToSwapchain(RhiTexture& sourceTexture, RhiSwapchain& swapchain) = 0;

        /// Sets the active graphics pipeline.
        virtual void SetPipeline(const RhiPipelineState& pipelineState) = 0;

        /// Sets the viewport rectangle for following draw calls.
        virtual void SetViewport(const RhiViewport& viewport) = 0;

        /// Sets the scissor rectangle for following draw calls.
        virtual void SetScissor(const RhiScissorRect& scissorRect) = 0;

        /// Binds a vertex buffer at the given slot.
        virtual void SetVertexBuffer(uint32_t slot, const RhiBuffer& buffer, uint32_t stride, uint64_t offset) = 0;

        /// Binds an index buffer for following indexed draw calls.
        virtual void SetIndexBuffer(const RhiBuffer& buffer, RhiIndexFormat format, uint64_t offset) = 0;

        /// Binds a uniform/constant buffer to one shader stage.
        virtual void SetUniformBuffer(RhiShaderStage stage, uint32_t slot, const RhiBuffer& buffer, uint64_t offset, uint64_t size) = 0;

        /// Binds a sampled texture to one shader stage.
        virtual void SetTexture(RhiShaderStage stage, uint32_t slot, const RhiTexture& texture) = 0;

        /// Binds a sampler state to one shader stage.
        virtual void SetSampler(RhiShaderStage stage, uint32_t slot, const RhiSampler& sampler) = 0;

        /// Issues a non-indexed draw call.
        virtual void Draw(uint32_t vertexCount, uint32_t firstVertex) = 0;

        /// Issues an indexed draw call.
        virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) = 0;

        /// Returns the backend-native render encoder/command encoder for the active render pass when available.
        [[nodiscard]] virtual void* GetNativeRenderEncoderHandle() const noexcept
        {
            return nullptr;
        }

        /// Returns the backend-native command buffer for the active submission when available.
        [[nodiscard]] virtual void* GetNativeCommandBufferHandle() const noexcept
        {
            return nullptr;
        }
    };

    /// Opaque CPU and GPU descriptor handle values used by native renderer integrations.
    struct RhiNativeShaderResourceDescriptor
    {
        uint64_t cpuHandle = 0;
        uint64_t gpuHandle = 0;
    };

    /// Allocates native shader-resource descriptors from the heap bound by a platform renderer integration.
    class RhiNativeShaderResourceDescriptorAllocator
    {
    public:
        virtual ~RhiNativeShaderResourceDescriptorAllocator() = default;

        [[nodiscard]] virtual void* GetNativeHeapHandle() const noexcept = 0;
        [[nodiscard]] virtual bool Allocate(RhiNativeShaderResourceDescriptor& outDescriptor) = 0;
        virtual void Release(RhiNativeShaderResourceDescriptor descriptor) noexcept = 0;
    };

    /// Creates RHI objects and submits command lists to a backend graphics queue.
    class RhiDevice : public RhiObject
    {
    public:
        /// Returns the backend implemented by this device.
        [[nodiscard]] virtual RhiBackend GetBackend() const noexcept = 0;

        /// Returns the most recent backend error message, if any.
        [[nodiscard]] virtual const char* GetLastErrorMessage() const noexcept = 0;

        /// Creates a swapchain for a platform surface.
        [[nodiscard]] virtual std::unique_ptr<RhiSwapchain> CreateSwapchain(const RhiSwapchainDesc& desc) = 0;

        /// Creates a GPU buffer and optionally uploads initial data.
        [[nodiscard]] virtual std::unique_ptr<RhiBuffer> CreateBuffer(const RhiBufferDesc& desc) = 0;

        /// Updates a CPU-visible buffer range.
        virtual void UpdateBuffer(RhiBuffer& buffer, uint64_t offset, const void* data, uint64_t size, RhiBufferUpdateMode updateMode) = 0;

        /// Creates a texture resource and optionally uploads initial data.
        [[nodiscard]] virtual std::unique_ptr<RhiTexture> CreateTexture(const RhiTextureDesc& desc) = 0;

        /// Creates a sampler state.
        [[nodiscard]] virtual std::unique_ptr<RhiSampler> CreateSampler(const RhiSamplerDesc& desc) = 0;

        /// Compiles or loads a shader module for this backend.
        [[nodiscard]] virtual std::unique_ptr<RhiShaderModule> CreateShaderModule(const RhiShaderModuleDesc& desc) = 0;

        /// Creates immutable graphics pipeline state.
        [[nodiscard]] virtual std::unique_ptr<RhiPipelineState> CreateGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) = 0;

        /// Creates a command list object compatible with this device.
        [[nodiscard]] virtual std::unique_ptr<RhiCommandList> CreateCommandList() = 0;

        /// Creates a fence object compatible with this device's graphics queue.
        [[nodiscard]] virtual std::unique_ptr<RhiFence> CreateFence(uint64_t initialValue = 0) = 0;

        /// Submits a recorded command list and optionally signals a completion fence after the queued work.
        [[nodiscard]] virtual bool Submit(RhiCommandList& commandList, RhiFence* completionFence = nullptr, uint64_t completionValue = 0) = 0;

        /// Blocks until all previously submitted work is complete.
        virtual void WaitIdle() = 0;

        /// Returns the backend-native device handle when available.
        /// For example: ID3D11Device* on D3D11, ID3D12Device* on D3D12.
        [[nodiscard]] virtual void* GetNativeDeviceHandle() const noexcept
        {
            return nullptr;
        }

        /// Returns the backend-native immediate context handle when available.
        /// For example: ID3D11DeviceContext* on D3D11.
        [[nodiscard]] virtual void* GetNativeImmediateContextHandle() const noexcept
        {
            return nullptr;
        }

        /// Returns the backend-native graphics queue handle when available.
        /// For example: ID3D12CommandQueue* on D3D12.
        [[nodiscard]] virtual void* GetNativeGraphicsQueueHandle() const noexcept
        {
            return nullptr;
        }

        /// Returns the native shader-resource descriptor allocator used by renderer integrations when available.
        [[nodiscard]] virtual RhiNativeShaderResourceDescriptorAllocator* GetNativeShaderResourceDescriptorAllocator() const noexcept
        {
            return nullptr;
        }
    };
} // namespace ve::rhi
