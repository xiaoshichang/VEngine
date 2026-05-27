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

        /// Begins rendering into the current back buffer of a swapchain.
        [[nodiscard]] virtual bool BeginRenderPass(RhiSwapchain& swapchain, const RhiRenderPassDesc& desc) = 0;

        /// Ends the active render pass.
        virtual void EndRenderPass() = 0;

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

        /// Binds a constant/uniform buffer for the given shader stage and logical binding slot.
        virtual void SetUniformBuffer(RhiShaderStage stage,
                                      uint32_t slot,
                                      const RhiBuffer& buffer,
                                      uint64_t offset,
                                      uint64_t size) = 0;

        /// Binds a sampled texture for the given shader stage and logical binding slot.
        virtual void SetTexture(RhiShaderStage stage, uint32_t slot, const RhiTexture& texture) = 0;

        /// Issues a non-indexed draw call.
        virtual void Draw(uint32_t vertexCount, uint32_t firstVertex) = 0;

        /// Issues an indexed draw call.
        virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) = 0;
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

        /// Creates a texture resource and optionally uploads initial data.
        [[nodiscard]] virtual std::unique_ptr<RhiTexture> CreateTexture(const RhiTextureDesc& desc) = 0;

        /// Compiles or loads a shader module for this backend.
        [[nodiscard]] virtual std::unique_ptr<RhiShaderModule> CreateShaderModule(const RhiShaderModuleDesc& desc) = 0;

        /// Creates immutable graphics pipeline state.
        [[nodiscard]] virtual std::unique_ptr<RhiPipelineState>
        CreateGraphicsPipeline(const RhiGraphicsPipelineDesc& desc) = 0;

        /// Creates a command list object compatible with this device.
        [[nodiscard]] virtual std::unique_ptr<RhiCommandList> CreateCommandList() = 0;

        /// Creates a fence object compatible with this device's graphics queue.
        [[nodiscard]] virtual std::unique_ptr<RhiFence> CreateFence(uint64_t initialValue = 0) = 0;

        /// Signals a fence from this device's graphics queue.
        [[nodiscard]] virtual bool SignalFence(RhiFence& fence, uint64_t value) = 0;

        /// Submits a recorded command list to the graphics queue.
        [[nodiscard]] virtual bool Submit(RhiCommandList& commandList) = 0;

        /// Blocks until all previously submitted work is complete.
        virtual void WaitIdle() = 0;
    };
} // namespace ve::rhi
