#pragma once

#include <cstdint>

namespace ve::rhi
{
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

/// Describes a small set of cross-backend texture and vertex formats.
enum class RhiFormat
{
    Unknown,
    Rgba8Unorm,
    Bgra8Unorm,
    Rgb32Float,
};

/// Describes how a render target should be loaded at render-pass begin.
enum class RhiLoadAction
{
    Load,
    Clear,
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

/// Describes a render pass targeting the current swapchain image.
struct RhiRenderPassDesc
{
    RhiLoadAction colorLoadAction = RhiLoadAction::Clear;
    RhiStoreAction colorStoreAction = RhiStoreAction::Store;
    RhiColor clearColor = {};
};

/// Describes initial data and usage for a GPU buffer.
struct RhiBufferDesc
{
    uint64_t size = 0;
    RhiBufferUsage usage = RhiBufferUsage::Vertex;
    const void* initialData = nullptr;
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

/// Describes immutable graphics pipeline state.
struct RhiGraphicsPipelineDesc
{
    const RhiShaderModule* vertexShader = nullptr;
    const RhiShaderModule* fragmentShader = nullptr;
    RhiVertexLayoutDesc vertexLayout = {};
    RhiPrimitiveTopology topology = RhiPrimitiveTopology::TriangleList;
    RhiFormat colorFormat = RhiFormat::Bgra8Unorm;
    const char* debugName = nullptr;
};
}
