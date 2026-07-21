#pragma once

#include <cstdint>

namespace ve::rhi
{
    class RhiTexture;

    inline constexpr uint32_t RhiMaxColorAttachments = 4;

    /// Identifies the native graphics backend used by an RHI device.
    enum class RhiBackend
    {
        D3D11,
        D3D12,
        Metal,
    };

    /// Identifies the shader stage of a shader module.
    enum class RhiShaderStage
    {
        Vertex,
        Fragment,
    };

    /// Identifies one shader-visible resource category declared by a graphics pipeline.
    enum class RhiPipelineResourceKind
    {
        UniformBuffer,
        SampledTexture,
        Sampler,
    };

    /// Describes the primitive topology consumed by a graphics pipeline.
    enum class RhiPrimitiveTopology
    {
        TriangleList,
        LineList,
    };

    enum class RhiFillMode
    {
        Solid,
        Wireframe,
    };

    /// Selects the multiplier applied to a source or destination term before a blend operation.
    enum class RhiBlendFactor
    {
        /// Multiplies the term by zero.
        Zero,
        /// Multiplies the term by one.
        One,
        /// Multiplies the term by the source color.
        SourceColor,
        /// Multiplies the term by one minus the source color.
        OneMinusSourceColor,
        /// Multiplies every component of the term by the source alpha.
        SourceAlpha,
        /// Multiplies every component of the term by one minus the source alpha.
        OneMinusSourceAlpha,
        /// Multiplies the term by the destination color already stored in the render target.
        DestinationColor,
        /// Multiplies the term by one minus the destination color.
        OneMinusDestinationColor,
        /// Multiplies every component of the term by the destination alpha.
        DestinationAlpha,
        /// Multiplies every component of the term by one minus the destination alpha.
        OneMinusDestinationAlpha,
    };

    /// Combines the source and destination terms after their blend factors have been applied.
    enum class RhiBlendOperation
    {
        /// Adds the source and destination terms.
        Add,
        /// Subtracts the destination term from the source term.
        Subtract,
        /// Subtracts the source term from the destination term.
        ReverseSubtract,
        /// Selects the component-wise minimum of the source and destination values.
        Min,
        /// Selects the component-wise maximum of the source and destination values.
        Max,
    };

    /// Selects which triangle faces are discarded before rasterization.
    enum class RhiCullMode
    {
        /// Rasterizes both front-facing and back-facing triangles.
        None,
        /// Discards front-facing triangles.
        Front,
        /// Discards back-facing triangles.
        Back,
    };

    /// Compares an incoming or reference value on the left with a stored destination value on the right.
    enum class RhiCompareFunction
    {
        /// Never passes.
        Never,
        /// Passes when the left value is less than the right value.
        Less,
        /// Passes when both values are equal.
        Equal,
        /// Always passes.
        Always,
        /// Passes when the left value is less than or equal to the right value.
        LessEqual,
        /// Passes when the left value is greater than the right value.
        Greater,
        /// Passes when both values are not equal.
        NotEqual,
        /// Passes when the left value is greater than or equal to the right value.
        GreaterEqual,
    };

    /// Selects how a stored stencil value is modified after a stencil or depth test event.
    enum class RhiStencilOperation
    {
        /// Leaves the current stencil value unchanged.
        Keep,
        /// Writes zero.
        Zero,
        /// Writes the current stencil reference value.
        Replace,
        /// Increments and saturates at the maximum representable stencil value.
        IncrementClamp,
        /// Decrements and saturates at zero.
        DecrementClamp,
        /// Inverts every bit in the current stencil value.
        Invert,
        /// Increments and wraps from the maximum representable value to zero.
        IncrementWrap,
        /// Decrements and wraps from zero to the maximum representable value.
        DecrementWrap,
    };

    /// Selects spatial and mip-level filtering for texture sampling.
    enum class RhiSamplerFilter
    {
        /// Uses the nearest texel and nearest mip level.
        Point,
        /// Linearly filters texels within one mip level and selects the nearest mip level.
        Bilinear,
        /// Linearly filters texels and blends between the two nearest mip levels.
        Trilinear,
        /// Uses anisotropic filtering to improve samples viewed at oblique angles.
        Anisotropic,
    };

    /// Selects how texture coordinates outside the normalized texture extent are resolved.
    enum class RhiSamplerAddressMode
    {
        /// Repeats the texture at every integer boundary.
        Wrap,
        /// Repeats the texture and mirrors every second repetition.
        Mirror,
        /// Clamps coordinates to the nearest texture edge.
        Clamp,
        /// Returns the sampler's configured border color when sampling outside the texture extent.
        Border,
    };

    /// Selects whether sampling returns texture values or comparison-test results.
    enum class RhiSamplerReductionMode
    {
        /// Filters and returns the sampled texture values.
        Standard,
        /// Compares samples with a reference value using the sampler comparison function, then filters the comparison results.
        Comparison,
    };

    /// Describes a small set of cross-backend texture, render-target, depth, and vertex formats.
    enum class RhiFormat
    {
        /// No format is specified. Used as an invalid or uninitialized format value.
        Unknown,
        /// Four 8-bit unsigned normalized channels in red, green, blue, alpha order. Integer values map to the floating-point range [0, 1].
        Rgba8Unorm,
        /// Four 8-bit unsigned normalized channels in blue, green, red, alpha order. Integer values map to the floating-point range [0, 1].
        Bgra8Unorm,
        /// Three 32-bit floating-point channels in red, green, blue order.
        Rgb32Float,
        /// One 32-bit floating-point depth channel.
        Depth32Float,
    };

    enum class RhiIndexFormat
    {
        UInt16,
        UInt32,
    };

    /// Describes the dimensionality of an RHI texture resource.
    enum class RhiTextureDimension
    {
        Texture2D,
    };

    /// Describes how an attachment is initialized when a render pass begins.
    enum class RhiLoadAction
    {
        /// Preserves and exposes the attachment contents produced before this render pass.
        Load,
        /// Initializes the attachment with the clear value specified by the render-pass descriptor.
        Clear,
        /// Does not preserve previous contents. The attachment contents are undefined until written by this render pass.
        DontCare,
    };

    /// Describes whether an attachment's contents remain valid after a render pass ends.
    enum class RhiStoreAction
    {
        /// Preserves the rendered contents for later passes, presentation, or readback.
        Store,
        /// Allows the backend to discard the rendered contents; subsequent attachment contents are undefined.
        DontCare,
    };

    /// Describes intended usage for a buffer.
    enum class RhiBufferUsage : uint32_t
    {
        Vertex = 1 << 0,
        Index = 1 << 1,
        Uniform = 1 << 2,
    };

    /// Describes intended usage for a texture.
    enum class RhiTextureUsage : uint32_t
    {
        Sampled = 1 << 0,
        RenderTarget = 1 << 1,
        DepthStencil = 1 << 2,
    };

    /// Stores a two-dimensional unsigned extent.
    struct RhiExtent2D
    {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /// Stores an RGBA color in linear floating-point space.
    struct RhiColor
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;

        [[nodiscard]] bool operator==(const RhiColor&) const noexcept = default;
    };

    /// Describes a viewport rectangle and depth range.
    struct RhiViewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    /// Describes an integer scissor rectangle.
    struct RhiScissorRect
    {
        int32_t x = 0;
        int32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /// Describes an integer render area inside pass attachments.
    struct RhiRenderArea
    {
        int32_t x = 0;
        int32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    inline constexpr uint8_t RhiColorWriteRed = 1 << 0;
    inline constexpr uint8_t RhiColorWriteGreen = 1 << 1;
    inline constexpr uint8_t RhiColorWriteBlue = 1 << 2;
    inline constexpr uint8_t RhiColorWriteAlpha = 1 << 3;
    inline constexpr uint8_t RhiColorWriteAll = RhiColorWriteRed | RhiColorWriteGreen | RhiColorWriteBlue | RhiColorWriteAlpha;

    struct RhiBlendRenderTargetDesc
    {
        bool blendEnabled = false;
        RhiBlendFactor sourceColorBlendFactor = RhiBlendFactor::One;
        RhiBlendFactor destinationColorBlendFactor = RhiBlendFactor::Zero;
        RhiBlendOperation colorBlendOperation = RhiBlendOperation::Add;
        RhiBlendFactor sourceAlphaBlendFactor = RhiBlendFactor::One;
        RhiBlendFactor destinationAlphaBlendFactor = RhiBlendFactor::Zero;
        RhiBlendOperation alphaBlendOperation = RhiBlendOperation::Add;
        uint8_t colorWriteMask = RhiColorWriteAll;
    };

    struct RhiStencilFaceDesc
    {
        RhiStencilOperation failOperation = RhiStencilOperation::Keep;
        RhiStencilOperation depthFailOperation = RhiStencilOperation::Keep;
        RhiStencilOperation passOperation = RhiStencilOperation::Keep;
        RhiCompareFunction compareFunction = RhiCompareFunction::Always;
    };

    struct RhiSamplerDesc
    {
        RhiSamplerFilter filter = RhiSamplerFilter::Bilinear;
        RhiSamplerAddressMode addressU = RhiSamplerAddressMode::Wrap;
        RhiSamplerAddressMode addressV = RhiSamplerAddressMode::Wrap;
        RhiSamplerAddressMode addressW = RhiSamplerAddressMode::Wrap;
        RhiSamplerReductionMode reductionMode = RhiSamplerReductionMode::Standard;
        RhiCompareFunction comparisonFunction = RhiCompareFunction::LessEqual;
        float mipBias = 0.0f;
        uint32_t maxAnisotropy = 1;
        RhiColor borderColor = {};
        float minLod = 0.0f;
        float maxLod = 3.402823466e+38f;
    };

    /// Describes how a swapchain should be created for a native surface.
    struct RhiSwapchainDesc
    {
        void* nativeWindow = nullptr;
        void* nativeLayer = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        RhiFormat colorFormat = RhiFormat::Bgra8Unorm;
        uint32_t bufferCount = 2;
        const char* debugName = nullptr;
    };

    /// Resolved color attachment consumed when beginning one native render pass.
    ///
    /// texture may be null only when the enclosing render-pass packet explicitly selects the swapchain. Logical
    /// resource declarations and dependency information belong to the frame graph and are intentionally absent here.
    struct RhiRenderPassColorAttachmentInfo
    {
        RhiTexture* texture = nullptr;
        RhiLoadAction loadAction = RhiLoadAction::Clear;
        RhiStoreAction storeAction = RhiStoreAction::Store;
        RhiColor clearColor = {};
    };

    /// Resolved depth attachment consumed when beginning one native render pass.
    struct RhiRenderPassDepthAttachmentInfo
    {
        RhiTexture* texture = nullptr;
        RhiLoadAction loadAction = RhiLoadAction::Clear;
        RhiStoreAction storeAction = RhiStoreAction::Store;
        float clearDepth = 1.0f;
        bool readOnly = false;
    };

    /// Physical parameters needed to begin one native render pass.
    ///
    /// The current RHI supports zero or one color attachment and one optional depth attachment. Frame-graph setup owns the
    /// logical attachment declarations and resolves them into this short-lived execution packet.
    struct RhiRenderPassBeginInfo
    {
        const char* debugName = nullptr;
        RhiRenderPassColorAttachmentInfo colorAttachment = {};
        RhiRenderPassDepthAttachmentInfo depthAttachment = {};
        bool hasColorAttachment = true;
        bool colorAttachmentIsSwapchain = true;
        bool hasDepthAttachment = false;
    };

    /// Describes the intended CPU/GPU access pattern of a buffer.
    enum class RhiBufferMemoryUsage
    {
        /// Optimized for GPU access. The CPU cannot update the buffer through RhiDevice::UpdateBuffer; contents are normally supplied at creation or
        /// through a backend upload path.
        GpuOnly,
        /// CPU-writable memory consumed by the GPU. Supports RhiDevice::UpdateBuffer and is intended for frequently updated upload or dynamic data.
        CpuToGpu,
    };

    /// Selects how an UpdateBuffer write relates to existing data in a CpuToGpu buffer.
    enum class RhiBufferUpdateMode
    {
        /// The previous buffer contents may be invalidated. Use for the first write after the entire buffer is safe to reuse; on APIs such as D3D11 this
        /// may rename or discard the whole resource.
        Discard,
        /// Preserves other ranges. The caller guarantees that the written range does not overlap data that an in-flight GPU submission may still read.
        NoOverwrite,
    };

    /// Describes initial data and usage for a GPU buffer.
    struct RhiBufferDesc
    {
        uint64_t size = 0;
        RhiBufferUsage usage = RhiBufferUsage::Vertex;
        RhiBufferMemoryUsage memoryUsage = RhiBufferMemoryUsage::GpuOnly;
        const void* initialData = nullptr;
        const char* debugName = nullptr;
    };

    /// Describes a texture resource.
    struct RhiTextureDesc
    {
        RhiTextureDimension dimension = RhiTextureDimension::Texture2D;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint32_t mipLevelCount = 1;
        RhiFormat format = RhiFormat::Rgba8Unorm;
        RhiTextureUsage usage = RhiTextureUsage::Sampled;
        const void* initialData = nullptr;
        uint64_t initialDataSize = 0;
        uint32_t initialDataRowPitch = 0;
        const char* debugName = nullptr;
    };

    enum class RhiShaderCodeFormat
    {
        Source,
        Bytecode,
    };

    /// Describes source code or precompiled bytecode used to create a shader module.
    struct RhiShaderModuleDesc
    {
        RhiShaderStage stage = RhiShaderStage::Vertex;
        RhiShaderCodeFormat codeFormat = RhiShaderCodeFormat::Source;
        const char* source = nullptr;
        const void* bytecode = nullptr;
        uint64_t bytecodeSize = 0;
        const char* entryPoint = nullptr;
        const char* debugName = nullptr;
    };

    /// Describes one vertex attribute in an interleaved vertex buffer.
    struct RhiVertexAttributeDesc
    {
        const char* semanticName = nullptr;
        uint32_t semanticIndex = 0;
        RhiFormat format = RhiFormat::Unknown;
        uint32_t offset = 0;
    };

    /// Describes the vertex input layout for a graphics pipeline.
    struct RhiVertexLayoutDesc
    {
        const RhiVertexAttributeDesc* attributes = nullptr;
        uint32_t attributeCount = 0;
        uint32_t stride = 0;
    };

    /// Declares one shader-visible pipeline binding.
    struct RhiPipelineResourceBindingDesc
    {
        RhiPipelineResourceKind kind = RhiPipelineResourceKind::UniformBuffer;
        RhiShaderStage stage = RhiShaderStage::Vertex;
        uint32_t slot = 0;
    };

    /// Declares every shader-visible resource binding used by a graphics pipeline.
    struct RhiPipelineResourceLayoutDesc
    {
        const RhiPipelineResourceBindingDesc* bindings = nullptr;
        uint32_t bindingCount = 0;
    };

    class RhiShaderModule;

    struct RhiBlendStateDesc
    {
        bool alphaToCoverageEnabled = false;
        bool independentBlendEnabled = false;
        RhiBlendRenderTargetDesc renderTargets[RhiMaxColorAttachments] = {};
    };

    struct RhiRasterizerStateDesc
    {
        RhiFillMode fillMode = RhiFillMode::Solid;
        RhiCullMode cullMode = RhiCullMode::Back;
        bool frontCounterClockwise = false;
        bool depthClipEnabled = true;
        bool scissorEnabled = false;
        bool multisampleEnabled = false;
        bool antialiasedLineEnabled = false;
        int32_t depthBias = 0;
        float depthBiasClamp = 0.0f;
        float slopeScaledDepthBias = 0.0f;
    };

    struct RhiDepthStencilStateDesc
    {
        bool depthTestEnabled = false;
        bool depthWriteEnabled = false;
        RhiCompareFunction depthCompareFunction = RhiCompareFunction::LessEqual;
        bool stencilEnabled = false;
        uint8_t stencilReadMask = 0xFF;
        uint8_t stencilWriteMask = 0xFF;
        RhiStencilFaceDesc frontFace = {};
        RhiStencilFaceDesc backFace = {};
    };

    struct RhiBoundShaderStateDesc
    {
        const RhiShaderModule* vertexShader = nullptr;
        const RhiShaderModule* fragmentShader = nullptr;
        RhiVertexLayoutDesc vertexDeclaration = {};
    };

    /// Describes immutable graphics pipeline state.
    struct RhiGraphicsPipelineDesc
    {
        RhiBlendStateDesc blendState = {};
        RhiRasterizerStateDesc rasterizerState = {};
        RhiDepthStencilStateDesc depthStencilState = {};
        RhiBoundShaderStateDesc boundShaderState = {};
        RhiPipelineResourceLayoutDesc resourceLayout = {};
        RhiPrimitiveTopology primitiveType = RhiPrimitiveTopology::TriangleList;
        uint32_t colorAttachmentCount = 1;
        RhiFormat colorFormat = RhiFormat::Bgra8Unorm;
        RhiFormat depthFormat = RhiFormat::Depth32Float;
        const char* debugName = nullptr;
    };
} // namespace ve::rhi
