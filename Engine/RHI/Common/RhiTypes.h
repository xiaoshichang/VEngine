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

    /// Describes the primitive topology consumed by a graphics pipeline.
    enum class RhiPrimitiveTopology
    {
        TriangleList,
    };

    enum class RhiFillMode
    {
        Solid,
        Wireframe,
    };

    enum class RhiBlendFactor
    {
        Zero,
        One,
        SourceColor,
        OneMinusSourceColor,
        SourceAlpha,
        OneMinusSourceAlpha,
        DestinationColor,
        OneMinusDestinationColor,
        DestinationAlpha,
        OneMinusDestinationAlpha,
    };

    enum class RhiBlendOperation
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };

    enum class RhiCullMode
    {
        None,
        Front,
        Back,
    };

    enum class RhiCompareFunction
    {
        Never,
        Less,
        Equal,
        Always,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
    };

    enum class RhiStencilOperation
    {
        Keep,
        Zero,
        Replace,
        IncrementClamp,
        DecrementClamp,
        Invert,
        IncrementWrap,
        DecrementWrap,
    };

    enum class RhiSamplerFilter
    {
        Point,
        Bilinear,
        Trilinear,
        Anisotropic,
    };

    enum class RhiSamplerAddressMode
    {
        Wrap,
        Mirror,
        Clamp,
        Border,
    };

    enum class RhiSamplerReductionMode
    {
        Standard,
        Comparison,
    };

    /// Describes a small set of cross-backend texture and vertex formats.
    enum class RhiFormat
    {
        Unknown,
        Rgba8Unorm,
        Bgra8Unorm,
        Rgb32Float,
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

    /// Describes how a render target should be loaded at render-pass begin.
    enum class RhiLoadAction
    {
        Load,
        Clear,
        DontCare,
    };

    /// Describes how a render target should be stored at render-pass end.
    enum class RhiStoreAction
    {
        Store,
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

    /// Selects one texture subresource used by a render-pass attachment.
    struct RhiTextureSubresource
    {
        uint32_t mipLevel = 0;
        uint32_t arraySlice = 0;
    };

    /// Stores depth/stencil clear values in API-neutral form.
    struct RhiDepthStencilClearValue
    {
        float depth = 1.0f;
        uint32_t stencil = 0;
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

    /// Describes one color attachment used by a render pass.
    ///
    /// texture may be null when CommandList::BeginRenderPass targets a swapchain and should use its current back
    /// buffer for this attachment.
    struct RhiRenderPassColorAttachmentDesc
    {
        RhiTexture* texture = nullptr;
        RhiTextureSubresource subresource = {};
        RhiLoadAction loadAction = RhiLoadAction::Clear;
        RhiStoreAction storeAction = RhiStoreAction::Store;
        RhiColor clearColor = {};
    };

    /// Describes the optional depth/stencil attachment used by a render pass.
    struct RhiRenderPassDepthStencilAttachmentDesc
    {
        RhiTexture* texture = nullptr;
        RhiTextureSubresource subresource = {};
        RhiLoadAction depthLoadAction = RhiLoadAction::Clear;
        RhiStoreAction depthStoreAction = RhiStoreAction::Store;
        RhiLoadAction stencilLoadAction = RhiLoadAction::DontCare;
        RhiStoreAction stencilStoreAction = RhiStoreAction::DontCare;
        RhiDepthStencilClearValue clearValue = {};
        bool depthReadOnly = false;
        bool stencilReadOnly = false;
    };

    /// Describes a render pass attachment set and its load/store behavior.
    struct RhiRenderPassDesc
    {
        const char* debugName = nullptr;
        RhiRenderArea renderArea = {};
        RhiRenderPassColorAttachmentDesc colorAttachments[RhiMaxColorAttachments] = {};
        uint32_t colorAttachmentCount = 0;
        RhiRenderPassDepthStencilAttachmentDesc depthStencilAttachment = {};
        bool hasDepthStencilAttachment = false;
    };

    /// Describes initial data and usage for a GPU buffer.
    struct RhiBufferDesc
    {
        uint64_t size = 0;
        RhiBufferUsage usage = RhiBufferUsage::Vertex;
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

    /// Describes source code and entry point for creating a shader module.
    struct RhiShaderModuleDesc
    {
        RhiShaderStage stage = RhiShaderStage::Vertex;
        const char* source = nullptr;
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
        RhiPrimitiveTopology primitiveType = RhiPrimitiveTopology::TriangleList;
        RhiFormat colorFormat = RhiFormat::Bgra8Unorm;
        RhiFormat depthFormat = RhiFormat::Depth32Float;
        const char* debugName = nullptr;
    };
} // namespace ve::rhi
