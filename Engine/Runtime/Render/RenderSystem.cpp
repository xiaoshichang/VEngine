#include "Engine/Runtime/Render/RenderSystem.h"

#if VE_PLATFORM_WINDOWS && VE_ENABLE_D3D11
#include "Engine/RHI/D3D11/D3D11Rhi.h"
#endif
#if VE_PLATFORM_WINDOWS && VE_ENABLE_D3D12
#include "Engine/RHI/D3D12/D3D12Rhi.h"
#endif
#if VE_ENABLE_METAL
#include "Engine/RHI/Metal/MetalRhi.h"
#endif

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/ScopeExit.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/RenderCommandQueue.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/Synchronization.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ve
{
    namespace
    {
        struct RenderVertex
        {
            Float32 position[3] = {};
            Float32 normal[3] = {};
            Float32 color[3] = {};
        };

        struct DrawUniformData
        {
            Float32 modelViewProjection[16] = {};
            Float32 worldMatrix[16] = {};
            Float32 baseColor[4] = {};
            Float32 lightDirectionIntensity[4] = {};
            Float32 lightColorAmbient[4] = {};
        };
        static_assert((sizeof(DrawUniformData) % 16) == 0,
                      "Draw uniform data must satisfy D3D constant buffer alignment.");

        struct EditorUiUniformData
        {
            Float32 projection[16] = {};
        };
        static_assert((sizeof(EditorUiUniformData) % 16) == 0,
                      "Editor UI uniform data must satisfy D3D constant buffer alignment.");

        struct RenderSceneMeshResource
        {
            std::unique_ptr<rhi::RhiBuffer> vertexBuffer;
            UInt32 vertexCount = 0;
            ResourceRevision revision = 0;
        };

        struct RenderSceneMaterialResource
        {
            Vector3 baseColor = Vector3::One();
            ResourceRevision revision = 0;
        };

        struct ReusableRenderBuffer
        {
            std::unique_ptr<rhi::RhiBuffer> buffer;
            UInt64 capacity = 0;
            rhi::RhiBufferUsage usage = rhi::RhiBufferUsage::Vertex;
        };

        struct EditorViewportTexture
        {
            std::unique_ptr<rhi::RhiTexture> colorTexture;
            std::unique_ptr<rhi::RhiCommandList> commandList;
            std::vector<ReusableRenderBuffer> drawUniformBuffers;
            UInt32 width = 0;
            UInt32 height = 0;
            rhi::RhiFormat format = rhi::RhiFormat::Unknown;
        };

        struct RenderMeshResourceUpdate
        {
            ResourceId resourceId = InvalidResourceId;
            ResourceRevision revision = 0;
            std::vector<RenderVertex> vertices;
        };

        struct RenderMaterialResourceUpdate
        {
            ResourceId resourceId = InvalidResourceId;
            ResourceRevision revision = 0;
            Vector3 baseColor = Vector3::One();
        };

        struct RenderResourceRegistryUpdate
        {
            std::vector<RenderMeshResourceUpdate> meshUpdates;
            std::vector<ResourceId> removedMeshes;
            std::vector<RenderMaterialResourceUpdate> materialUpdates;
            std::vector<ResourceId> removedMaterials;

            [[nodiscard]] bool IsEmpty() const noexcept
            {
                return meshUpdates.empty() && removedMeshes.empty() && materialUpdates.empty() &&
                       removedMaterials.empty();
            }
        };

        struct SceneDrawCommand
        {
            const RenderSceneMeshResource* mesh = nullptr;
            Matrix44 worldMatrix = Matrix44::Identity();
            Matrix44 modelViewProjection = Matrix44::Identity();
            Vector3 baseColor = Vector3::One();
        };

        const char* DrawHlslShaderSource = R"(
cbuffer DrawConstants : register(b0)
{
    row_major float4x4 modelViewProjection;
    row_major float4x4 worldMatrix;
    float4 baseColor;
    float4 lightDirectionIntensity;
    float4 lightColorAmbient;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(modelViewProjection, float4(input.position, 1.0f));

    float3 rawWorldNormal = mul(worldMatrix, float4(input.normal, 0.0f)).xyz;
    float normalLength = length(rawWorldNormal);
    float3 worldNormal = normalLength > 0.00001f ? rawWorldNormal / normalLength : float3(0.0f, 1.0f, 0.0f);
    float3 lightDirection = normalize(lightDirectionIntensity.xyz);
    float ndotl = max(0.0f, dot(worldNormal, -lightDirection));
    float3 surfaceColor = input.color * baseColor.rgb;
    output.color = saturate((surfaceColor * lightColorAmbient.a) +
                            (surfaceColor * lightColorAmbient.rgb * ndotl * lightDirectionIntensity.a));
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return float4(input.color, 1.0f);
}
)";

        const char* DrawMetalShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct DrawConstants
{
    float4x4 modelViewProjection;
    float4x4 worldMatrix;
    float4 baseColor;
    float4 lightDirectionIntensity;
    float4 lightColorAmbient;
};

struct VSInput
{
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float3 color [[attribute(2)]];
};

struct VSOutput
{
    float4 position [[position]];
    float3 color;
};

vertex VSOutput VSMain(VSInput input [[stage_in]], constant DrawConstants& constants [[buffer(16)]])
{
    VSOutput output;
    output.position = constants.modelViewProjection * float4(input.position, 1.0);

    float3 rawWorldNormal = (constants.worldMatrix * float4(input.normal, 0.0)).xyz;
    float normalLength = length(rawWorldNormal);
    float3 worldNormal = normalLength > 0.00001 ? rawWorldNormal / normalLength : float3(0.0, 1.0, 0.0);
    float3 lightDirection = normalize(constants.lightDirectionIntensity.xyz);
    float ndotl = max(0.0, dot(worldNormal, -lightDirection));
    float3 surfaceColor = input.color * constants.baseColor.xyz;
    output.color = clamp((surfaceColor * constants.lightColorAmbient.w) +
                         (surfaceColor * constants.lightColorAmbient.xyz * ndotl *
                          constants.lightDirectionIntensity.w),
                         0.0,
                         1.0);
    return output;
}

fragment float4 PSMain(VSOutput input [[stage_in]])
{
    return float4(input.color, 1.0);
}
)";

        const char* EditorUiHlslShaderSource = R"(
cbuffer UiConstants : register(b0)
{
    row_major float4x4 projection;
};

Texture2D fontTexture : register(t0);
SamplerState fontSampler : register(s0);

struct VSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(projection, float4(input.position, 0.0f, 1.0f));
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return input.color * fontTexture.Sample(fontSampler, input.uv);
}
)";

        const char* EditorUiMetalShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct UiConstants
{
    float4x4 projection;
};

struct VSInput
{
    float2 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
};

struct VSOutput
{
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex VSOutput VSMain(VSInput input [[stage_in]], constant UiConstants& constants [[buffer(16)]])
{
    VSOutput output;
    output.position = constants.projection * float4(input.position, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

fragment float4 PSMain(VSOutput input [[stage_in]], texture2d<float> fontTexture [[texture(0)]])
{
    constexpr sampler fontSampler(coord::normalized, address::clamp_to_edge, filter::linear);
    return input.color * fontTexture.sample(fontSampler, input.uv);
}
)";

        enum class RenderFrameSlotState
        {
            Free,
            Submitted,
            Rendering,
        };

        struct RenderFrameToken
        {
            UInt64 frameId = 0;
        };

        struct RenderFrameContext
        {
            UInt64 frameId = 0;
            RenderFrameSlotState state = RenderFrameSlotState::Free;
            ErrorCode lastError = ErrorCode::None;
            std::unique_ptr<rhi::RhiCommandList> commandList;
            std::vector<ReusableRenderBuffer> drawUniformBuffers;
        };
    } // namespace

    struct RenderSystemImpl
    {
        struct ThreadState
        {
            Thread ownedThread;
            Atomic<UInt64> renderThreadIdValue{0};
            AtomicBool initialized{false};
            AtomicBool stopRequested{false};
        };

        struct CommandState
        {
            Semaphore semaphore{0};
            RenderCommandQueue queue;
            AtomicBool acceptingCommands{false};
            AtomicSize activeSubmitCount{0};
        };

        struct GameThreadBindingState
        {
            Atomic<UInt64> gameThreadIdValue{0};
        };

        struct FrameRingState
        {
            UInt32 maxFramesInFlight = DefaultMaxRenderFramesInFlight;
            UInt64 nextRenderFrameId = 0;
            std::vector<RenderFrameContext> contexts;
        };

        struct RhiState
        {
            struct ScenePipelineResources
            {
                std::unique_ptr<rhi::RhiShaderModule> vertexShader;
                std::unique_ptr<rhi::RhiShaderModule> fragmentShader;
                std::unique_ptr<rhi::RhiPipelineState> pipelineState;
            };

            struct EditorUiPipelineResources
            {
                std::unique_ptr<rhi::RhiShaderModule> vertexShader;
                std::unique_ptr<rhi::RhiShaderModule> fragmentShader;
                std::unique_ptr<rhi::RhiPipelineState> pipelineState;
                std::unique_ptr<rhi::RhiTexture> fontTexture;
                std::unique_ptr<rhi::RhiCommandList> commandList;
                ReusableRenderBuffer vertexBuffer;
                ReusableRenderBuffer indexBuffer;
                ReusableRenderBuffer uniformBuffer;
            };

            Atomic<int> backendValue{-1};
            std::unique_ptr<rhi::RhiDevice> device;
            std::unique_ptr<rhi::RhiSwapchain> mainSwapchain;
            ScenePipelineResources scenePipeline;
            EditorUiPipelineResources editorUiPipeline;
            std::unordered_map<UInt64, EditorViewportTexture> editorViewportTextures;
            std::unordered_map<ResourceId, RenderSceneMeshResource> sceneMeshes;
            std::unordered_map<ResourceId, RenderSceneMaterialResource> sceneMaterials;
        };

        struct ResourceSynchronizationState
        {
            UInt64 resourceChangeSerial = 0;
            std::unordered_map<ResourceId, ResourceRevision> meshRevisions;
            std::unordered_map<ResourceId, ResourceRevision> materialRevisions;
        };

        ThreadState thread;
        CommandState commands;
        GameThreadBindingState gameThreadBinding;
        FrameRingState frameRing;
        RhiState rhi;
        ResourceSynchronizationState resourceSynchronization;
    };

    namespace
    {
        [[nodiscard]] const char* ToString(RenderBackend backend) noexcept
        {
            switch (backend)
            {
            case RenderBackend::D3D11:
                return "D3D11";
            case RenderBackend::D3D12:
                return "D3D12";
            case RenderBackend::Metal:
                return "Metal";
            }

            return "Unknown";
        }

        [[noreturn]] void TerminateRenderSystem(std::string_view message, ErrorCode errorCode)
        {
            VE_LOG_FATAL("RenderSystem {}: {}", message, ToString(errorCode));
            std::terminate();
        }

        [[nodiscard]] ThreadId GetBoundGameThreadId(const RenderSystemImpl& impl) noexcept
        {
            return ThreadId{impl.gameThreadBinding.gameThreadIdValue.load(std::memory_order_acquire)};
        }

        [[nodiscard]] bool CheckGameThreadAccess(const RenderSystemImpl& impl) noexcept
        {
            const ThreadId gameThreadId = GetBoundGameThreadId(impl);
            return gameThreadId.IsValid() && GetCurrentThreadId() == gameThreadId;
        }

        void ValidateGameThreadAccess(const RenderSystemImpl& impl, const char* functionName) noexcept
        {
            VE_ASSERT_MESSAGE(CheckGameThreadAccess(impl), functionName);
        }

        [[nodiscard]] ErrorCode ValidateSurfaceDesc(const RenderSurfaceDesc& desc)
        {
            if (desc.width == 0 || desc.height == 0)
            {
                return ErrorCode::InvalidArgument;
            }

            if (desc.bufferCount == 0)
            {
                return ErrorCode::InvalidArgument;
            }

            if (desc.nativeWindow == nullptr && desc.nativeLayer == nullptr)
            {
                return ErrorCode::InvalidArgument;
            }

            return ErrorCode::None;
        }

        [[nodiscard]] rhi::RhiSwapchainDesc ToRhiSwapchainDesc(const RenderSurfaceDesc& desc)
        {
            rhi::RhiSwapchainDesc rhiDesc = {};
            rhiDesc.nativeWindow = desc.nativeWindow;
            rhiDesc.nativeLayer = desc.nativeLayer;
            rhiDesc.width = desc.width;
            rhiDesc.height = desc.height;
            rhiDesc.colorFormat = desc.colorFormat;
            rhiDesc.bufferCount = desc.bufferCount;
            rhiDesc.debugName = "VEngineMainSwapchain";
            return rhiDesc;
        }

        [[nodiscard]] rhi::RhiTextureUsage CombineTextureUsage(rhi::RhiTextureUsage left,
                                                               rhi::RhiTextureUsage right) noexcept
        {
            return static_cast<rhi::RhiTextureUsage>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
        }

        [[nodiscard]] std::unique_ptr<rhi::RhiDevice> CreateRhiDevice(const RenderDeviceDesc& desc)
        {
            switch (desc.backend)
            {
            case RenderBackend::D3D11:
#if VE_PLATFORM_WINDOWS && VE_ENABLE_D3D11
                return rhi::CreateD3D11Device(desc.enableDebugDevice);
#else
                return nullptr;
#endif

            case RenderBackend::D3D12:
#if VE_PLATFORM_WINDOWS && VE_ENABLE_D3D12
                return rhi::CreateD3D12Device(desc.enableDebugDevice);
#else
                return nullptr;
#endif

            case RenderBackend::Metal:
#if VE_ENABLE_METAL
                return rhi::CreateMetalDevice(desc.enableDebugDevice);
#else
                return nullptr;
#endif
            }

            return nullptr;
        }

        [[nodiscard]] const char* GetDrawShaderSource(rhi::RhiBackend backend) noexcept
        {
            switch (backend)
            {
            case rhi::RhiBackend::Metal:
                return DrawMetalShaderSource;
            case rhi::RhiBackend::D3D11:
            case rhi::RhiBackend::D3D12:
                return DrawHlslShaderSource;
            }

            return DrawHlslShaderSource;
        }

        [[nodiscard]] const char* GetEditorUiShaderSource(rhi::RhiBackend backend) noexcept
        {
            switch (backend)
            {
            case rhi::RhiBackend::Metal:
                return EditorUiMetalShaderSource;
            case rhi::RhiBackend::D3D11:
            case rhi::RhiBackend::D3D12:
                return EditorUiHlslShaderSource;
            }

            return EditorUiHlslShaderSource;
        }

        [[nodiscard]] Matrix44 ToShaderMatrix(const RenderSystemImpl& impl, const Matrix44& matrix) noexcept
        {
            if (impl.rhi.device != nullptr && impl.rhi.device->GetBackend() == rhi::RhiBackend::Metal)
            {
                return matrix.Transposed();
            }

            return matrix;
        }

        void StoreMatrix(Float32* destination, const Matrix44& matrix) noexcept
        {
            const std::array<Float32, 16>& values = matrix.GetValues();
            for (SizeT index = 0; index < values.size(); ++index)
            {
                destination[index] = values[index];
            }
        }

        [[nodiscard]] Matrix44 BuildEditorUiProjectionMatrix(const EditorUiFrameData& frameData) noexcept
        {
            const Float32 left = frameData.displayPos[0];
            const Float32 right = frameData.displayPos[0] + frameData.displaySize[0];
            const Float32 top = frameData.displayPos[1];
            const Float32 bottom = frameData.displayPos[1] + frameData.displaySize[1];

            return Matrix44(std::array<Float32, 16>{
                2.0f / (right - left),
                0.0f,
                0.0f,
                (right + left) / (left - right),
                0.0f,
                2.0f / (top - bottom),
                0.0f,
                (top + bottom) / (bottom - top),
                0.0f,
                0.0f,
                0.5f,
                0.5f,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            });
        }

        [[nodiscard]] EditorUiUniformData BuildEditorUiUniformData(RenderSystemImpl& impl,
                                                                   const EditorUiFrameData& frameData) noexcept
        {
            EditorUiUniformData uniforms = {};
            StoreMatrix(uniforms.projection, ToShaderMatrix(impl, BuildEditorUiProjectionMatrix(frameData)));
            return uniforms;
        }

        [[nodiscard]] Vector3 NormalizeOrFallback(const Vector3& value, const Vector3& fallback) noexcept
        {
            const Vector3 normalized = value.Normalized();
            return normalized == Vector3::Zero() ? fallback : normalized;
        }

        [[nodiscard]] DrawUniformData BuildDrawUniformData(const RenderSystemImpl& impl,
                                                           const Matrix44& modelViewProjection,
                                                           const Matrix44& worldMatrix,
                                                           const Vector3& baseColor,
                                                           const Vector3& lightDirection,
                                                           const Vector3& lightColor,
                                                           Float32 lightIntensity,
                                                           Float32 ambientIntensity) noexcept
        {
            DrawUniformData uniforms = {};
            StoreMatrix(uniforms.modelViewProjection, ToShaderMatrix(impl, modelViewProjection));
            StoreMatrix(uniforms.worldMatrix, ToShaderMatrix(impl, worldMatrix));

            uniforms.baseColor[0] = baseColor.GetX();
            uniforms.baseColor[1] = baseColor.GetY();
            uniforms.baseColor[2] = baseColor.GetZ();
            uniforms.baseColor[3] = 1.0f;

            const Vector3 normalizedLightDirection = NormalizeOrFallback(lightDirection, Vector3(0.0f, -1.0f, 0.0f));
            uniforms.lightDirectionIntensity[0] = normalizedLightDirection.GetX();
            uniforms.lightDirectionIntensity[1] = normalizedLightDirection.GetY();
            uniforms.lightDirectionIntensity[2] = normalizedLightDirection.GetZ();
            uniforms.lightDirectionIntensity[3] = lightIntensity;

            uniforms.lightColorAmbient[0] = lightColor.GetX();
            uniforms.lightColorAmbient[1] = lightColor.GetY();
            uniforms.lightColorAmbient[2] = lightColor.GetZ();
            uniforms.lightColorAmbient[3] = ambientIntensity;

            return uniforms;
        }

        [[nodiscard]] DrawUniformData BuildSceneDrawUniformData(const RenderSystemImpl& impl,
                                                                const SceneRenderSnapshot& snapshot,
                                                                const SceneDrawCommand& drawCommand) noexcept
        {
            Vector3 lightDirection(0.0f, -1.0f, 0.0f);
            Vector3 lightColor = Vector3::One();
            Float32 lightIntensity = 0.0f;
            Float32 ambientIntensity = 1.0f;

            if (!snapshot.directionalLights.empty())
            {
                const SceneRenderDirectionalLight& light = snapshot.directionalLights.front();
                lightDirection = light.direction;
                lightColor = light.color;
                lightIntensity = light.intensity;
                ambientIntensity = 0.2f;
            }

            return BuildDrawUniformData(impl,
                                        drawCommand.modelViewProjection,
                                        drawCommand.worldMatrix,
                                        drawCommand.baseColor,
                                        lightDirection,
                                        lightColor,
                                        lightIntensity,
                                        ambientIntensity);
        }

        [[nodiscard]] bool EnsureReusableBuffer(RenderSystemImpl& impl,
                                                ReusableRenderBuffer& reusableBuffer,
                                                rhi::RhiBufferUsage usage,
                                                const void* data,
                                                UInt64 size,
                                                const char* debugName)
        {
            if (impl.rhi.device == nullptr || data == nullptr || size == 0)
            {
                return false;
            }

            if (reusableBuffer.buffer == nullptr || reusableBuffer.capacity < size || reusableBuffer.usage != usage)
            {
                rhi::RhiBufferDesc bufferDesc = {};
                bufferDesc.size = size;
                bufferDesc.usage = usage;
                bufferDesc.initialData = data;
                bufferDesc.debugName = debugName;

                reusableBuffer.buffer = impl.rhi.device->CreateBuffer(bufferDesc);
                reusableBuffer.capacity = reusableBuffer.buffer != nullptr ? size : 0;
                reusableBuffer.usage = usage;
                return reusableBuffer.buffer != nullptr;
            }

            return impl.rhi.device->UpdateBuffer(*reusableBuffer.buffer, data, size, 0);
        }

        void DestroyScenePipelineResources(RenderSystemImpl& impl)
        {
            for (RenderFrameContext& frameContext : impl.frameRing.contexts)
            {
                frameContext.commandList.reset();
                frameContext.drawUniformBuffers.clear();
                frameContext.lastError = ErrorCode::None;
            }

            impl.rhi.scenePipeline.pipelineState.reset();
            impl.rhi.scenePipeline.fragmentShader.reset();
            impl.rhi.scenePipeline.vertexShader.reset();
            impl.rhi.sceneMaterials.clear();
            impl.rhi.sceneMeshes.clear();
        }

        void DestroyEditorUiPipelineResources(RenderSystemImpl& impl)
        {
            impl.rhi.editorViewportTextures.clear();
            impl.rhi.editorUiPipeline.commandList.reset();
            impl.rhi.editorUiPipeline.uniformBuffer = {};
            impl.rhi.editorUiPipeline.indexBuffer = {};
            impl.rhi.editorUiPipeline.vertexBuffer = {};
            impl.rhi.editorUiPipeline.fontTexture.reset();
            impl.rhi.editorUiPipeline.pipelineState.reset();
            impl.rhi.editorUiPipeline.fragmentShader.reset();
            impl.rhi.editorUiPipeline.vertexShader.reset();
        }

        void ClearResourceSynchronizationState(RenderSystemImpl& impl)
        {
            impl.resourceSynchronization.resourceChangeSerial = 0;
            impl.resourceSynchronization.materialRevisions.clear();
            impl.resourceSynchronization.meshRevisions.clear();
        }

        [[nodiscard]] ErrorCode CreateEditorUiPipelineResources(RenderSystemImpl& impl)
        {
            VE_ASSERT_MESSAGE(impl.rhi.device != nullptr,
                              "CreateEditorUiPipelineResources requires an initialized RHI device.");
            VE_ASSERT_MESSAGE(impl.rhi.mainSwapchain != nullptr,
                              "CreateEditorUiPipelineResources requires an initialized main swapchain.");

            const char* shaderSource = GetEditorUiShaderSource(impl.rhi.device->GetBackend());

            rhi::RhiShaderModuleDesc vertexShaderDesc = {};
            vertexShaderDesc.stage = rhi::RhiShaderStage::Vertex;
            vertexShaderDesc.source = shaderSource;
            vertexShaderDesc.entryPoint = "VSMain";
            vertexShaderDesc.debugName = "RenderSystemEditorUiVertexShader";

            impl.rhi.editorUiPipeline.vertexShader = impl.rhi.device->CreateShaderModule(vertexShaderDesc);
            if (impl.rhi.editorUiPipeline.vertexShader == nullptr)
            {
                DestroyEditorUiPipelineResources(impl);
                return ErrorCode::PlatformError;
            }

            rhi::RhiShaderModuleDesc fragmentShaderDesc = {};
            fragmentShaderDesc.stage = rhi::RhiShaderStage::Fragment;
            fragmentShaderDesc.source = shaderSource;
            fragmentShaderDesc.entryPoint = "PSMain";
            fragmentShaderDesc.debugName = "RenderSystemEditorUiFragmentShader";

            impl.rhi.editorUiPipeline.fragmentShader = impl.rhi.device->CreateShaderModule(fragmentShaderDesc);
            if (impl.rhi.editorUiPipeline.fragmentShader == nullptr)
            {
                DestroyEditorUiPipelineResources(impl);
                return ErrorCode::PlatformError;
            }

            rhi::RhiVertexAttributeDesc attributes[3] = {};
            attributes[0].semanticName = "POSITION";
            attributes[0].semanticIndex = 0;
            attributes[0].format = rhi::RhiFormat::Rg32Float;
            attributes[0].offset = 0;
            attributes[1].semanticName = "TEXCOORD";
            attributes[1].semanticIndex = 0;
            attributes[1].format = rhi::RhiFormat::Rg32Float;
            attributes[1].offset = sizeof(Float32) * 2;
            attributes[2].semanticName = "COLOR";
            attributes[2].semanticIndex = 0;
            attributes[2].format = rhi::RhiFormat::Rgba8Unorm;
            attributes[2].offset = sizeof(Float32) * 4;

            rhi::RhiUniformBufferBindingDesc uniformBindings[1] = {};
            uniformBindings[0].stage = rhi::RhiShaderStage::Vertex;
            uniformBindings[0].slot = 0;

            rhi::RhiTextureBindingDesc textureBindings[1] = {};
            textureBindings[0].stage = rhi::RhiShaderStage::Fragment;
            textureBindings[0].slot = 0;

            rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
            pipelineDesc.vertexShader = impl.rhi.editorUiPipeline.vertexShader.get();
            pipelineDesc.fragmentShader = impl.rhi.editorUiPipeline.fragmentShader.get();
            pipelineDesc.vertexLayout.attributes = attributes;
            pipelineDesc.vertexLayout.attributeCount = 3;
            pipelineDesc.vertexLayout.stride = sizeof(EditorUiVertex);
            pipelineDesc.uniformBufferBindings = uniformBindings;
            pipelineDesc.uniformBufferBindingCount = 1;
            pipelineDesc.textureBindings = textureBindings;
            pipelineDesc.textureBindingCount = 1;
            pipelineDesc.topology = rhi::RhiPrimitiveTopology::TriangleList;
            pipelineDesc.colorFormat = impl.rhi.mainSwapchain->GetColorFormat();
            pipelineDesc.cullMode = rhi::RhiCullMode::None;
            pipelineDesc.enableAlphaBlending = true;
            pipelineDesc.enableScissor = true;
            pipelineDesc.debugName = "RenderSystemEditorUiPipeline";

            impl.rhi.editorUiPipeline.pipelineState = impl.rhi.device->CreateGraphicsPipeline(pipelineDesc);
            if (impl.rhi.editorUiPipeline.pipelineState == nullptr)
            {
                DestroyEditorUiPipelineResources(impl);
                return ErrorCode::PlatformError;
            }

            impl.rhi.editorUiPipeline.commandList = impl.rhi.device->CreateCommandList();
            if (impl.rhi.editorUiPipeline.commandList == nullptr)
            {
                DestroyEditorUiPipelineResources(impl);
                return ErrorCode::PlatformError;
            }

            return ErrorCode::None;
        }

        [[nodiscard]] std::unique_ptr<rhi::RhiTexture> CreateEditorUiFontTexture(RenderSystemImpl& impl,
                                                                                 const EditorUiFontAtlas& fontAtlas)
        {
            if (!fontAtlas.IsValid())
            {
                return nullptr;
            }

            rhi::RhiTextureDesc textureDesc = {};
            textureDesc.dimension = rhi::RhiTextureDimension::Texture2D;
            textureDesc.width = fontAtlas.width;
            textureDesc.height = fontAtlas.height;
            textureDesc.format = rhi::RhiFormat::Rgba8Unorm;
            textureDesc.usage = rhi::RhiTextureUsage::Sampled;
            textureDesc.initialData = fontAtlas.rgbaPixels.data();
            textureDesc.initialDataSize = fontAtlas.rgbaPixels.size();
            textureDesc.initialDataRowPitch = fontAtlas.width * 4;
            textureDesc.debugName = "RenderSystemEditorUiFontAtlas";
            return impl.rhi.device->CreateTexture(textureDesc);
        }

        [[nodiscard]] ErrorCode CreateScenePipelineResources(RenderSystemImpl& impl)
        {
            VE_ASSERT_MESSAGE(impl.rhi.device != nullptr,
                              "CreateScenePipelineResources requires an initialized RHI device.");
            VE_ASSERT_MESSAGE(impl.rhi.mainSwapchain != nullptr,
                              "CreateScenePipelineResources requires an initialized main swapchain.");

            const char* shaderSource = GetDrawShaderSource(impl.rhi.device->GetBackend());

            rhi::RhiShaderModuleDesc vertexShaderDesc = {};
            vertexShaderDesc.stage = rhi::RhiShaderStage::Vertex;
            vertexShaderDesc.source = shaderSource;
            vertexShaderDesc.entryPoint = "VSMain";
            vertexShaderDesc.debugName = "RenderSystemSceneVertexShader";

            impl.rhi.scenePipeline.vertexShader = impl.rhi.device->CreateShaderModule(vertexShaderDesc);
            if (impl.rhi.scenePipeline.vertexShader == nullptr)
            {
                DestroyScenePipelineResources(impl);
                return ErrorCode::PlatformError;
            }

            rhi::RhiShaderModuleDesc fragmentShaderDesc = {};
            fragmentShaderDesc.stage = rhi::RhiShaderStage::Fragment;
            fragmentShaderDesc.source = shaderSource;
            fragmentShaderDesc.entryPoint = "PSMain";
            fragmentShaderDesc.debugName = "RenderSystemSceneFragmentShader";

            impl.rhi.scenePipeline.fragmentShader = impl.rhi.device->CreateShaderModule(fragmentShaderDesc);
            if (impl.rhi.scenePipeline.fragmentShader == nullptr)
            {
                DestroyScenePipelineResources(impl);
                return ErrorCode::PlatformError;
            }

            rhi::RhiVertexAttributeDesc attributes[3] = {};
            attributes[0].semanticName = "POSITION";
            attributes[0].semanticIndex = 0;
            attributes[0].format = rhi::RhiFormat::Rgb32Float;
            attributes[0].offset = 0;
            attributes[1].semanticName = "NORMAL";
            attributes[1].semanticIndex = 0;
            attributes[1].format = rhi::RhiFormat::Rgb32Float;
            attributes[1].offset = sizeof(Float32) * 3;
            attributes[2].semanticName = "COLOR";
            attributes[2].semanticIndex = 0;
            attributes[2].format = rhi::RhiFormat::Rgb32Float;
            attributes[2].offset = sizeof(Float32) * 6;

            rhi::RhiUniformBufferBindingDesc uniformBindings[1] = {};
            uniformBindings[0].stage = rhi::RhiShaderStage::Vertex;
            uniformBindings[0].slot = 0;

            rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
            pipelineDesc.vertexShader = impl.rhi.scenePipeline.vertexShader.get();
            pipelineDesc.fragmentShader = impl.rhi.scenePipeline.fragmentShader.get();
            pipelineDesc.vertexLayout.attributes = attributes;
            pipelineDesc.vertexLayout.attributeCount = 3;
            pipelineDesc.vertexLayout.stride = sizeof(RenderVertex);
            pipelineDesc.uniformBufferBindings = uniformBindings;
            pipelineDesc.uniformBufferBindingCount = 1;
            pipelineDesc.topology = rhi::RhiPrimitiveTopology::TriangleList;
            pipelineDesc.colorFormat = impl.rhi.mainSwapchain->GetColorFormat();
            pipelineDesc.debugName = "RenderSystemScenePipeline";

            impl.rhi.scenePipeline.pipelineState = impl.rhi.device->CreateGraphicsPipeline(pipelineDesc);
            if (impl.rhi.scenePipeline.pipelineState == nullptr)
            {
                DestroyScenePipelineResources(impl);
                return ErrorCode::PlatformError;
            }

            for (RenderFrameContext& frameContext : impl.frameRing.contexts)
            {
                frameContext.commandList = impl.rhi.device->CreateCommandList();
                if (frameContext.commandList == nullptr)
                {
                    DestroyScenePipelineResources(impl);
                    return ErrorCode::PlatformError;
                }
            }

            return ErrorCode::None;
        }

        [[nodiscard]] RenderVertex ToRenderVertex(const MeshVertex& vertex) noexcept
        {
            const Vector3 position = vertex.position;
            const Vector3 normal = vertex.normal;
            const Vector3 color = vertex.color;
            return RenderVertex{{position.GetX(), position.GetY(), position.GetZ()},
                                {normal.GetX(), normal.GetY(), normal.GetZ()},
                                {color.GetX(), color.GetY(), color.GetZ()}};
        }

        [[nodiscard]] RenderMeshResourceUpdate BuildRenderMeshResourceUpdate(const MeshResource& mesh)
        {
            RenderMeshResourceUpdate update;
            update.resourceId = mesh.id;
            update.revision = mesh.revision;
            update.vertices.reserve(mesh.vertices.size());

            for (const MeshVertex& vertex : mesh.vertices)
            {
                update.vertices.push_back(ToRenderVertex(vertex));
            }

            return update;
        }

        [[nodiscard]] RenderMaterialResourceUpdate BuildRenderMaterialResourceUpdate(const MaterialResource& material)
        {
            return RenderMaterialResourceUpdate{material.id, material.revision, material.baseColor};
        }

        [[nodiscard]] RenderResourceRegistryUpdate
        BuildRenderResourceRegistryUpdate(RenderSystemImpl& impl, const ResourceManager& resourceManager)
        {
            RenderResourceRegistryUpdate update;
            RenderSystemImpl::ResourceSynchronizationState& synchronization = impl.resourceSynchronization;
            const UInt64 resourceChangeSerial = resourceManager.GetChangeSerial();

            if (synchronization.resourceChangeSerial == resourceChangeSerial)
            {
                return update;
            }

            std::unordered_set<ResourceId> liveMeshIds;
            resourceManager.ForEachMesh(
                [&update, &liveMeshIds, &synchronization](const MeshResource& mesh)
                {
                    if (mesh.id == InvalidResourceId)
                    {
                        return;
                    }

                    liveMeshIds.insert(mesh.id);
                    const auto submittedRevision = synchronization.meshRevisions.find(mesh.id);
                    if (submittedRevision == synchronization.meshRevisions.end() ||
                        submittedRevision->second != mesh.revision)
                    {
                        update.meshUpdates.push_back(BuildRenderMeshResourceUpdate(mesh));
                        synchronization.meshRevisions[mesh.id] = mesh.revision;
                    }
                });

            for (auto iter = synchronization.meshRevisions.begin(); iter != synchronization.meshRevisions.end();)
            {
                if (liveMeshIds.find(iter->first) == liveMeshIds.end())
                {
                    update.removedMeshes.push_back(iter->first);
                    iter = synchronization.meshRevisions.erase(iter);
                }
                else
                {
                    ++iter;
                }
            }

            std::unordered_set<ResourceId> liveMaterialIds;
            resourceManager.ForEachMaterial(
                [&update, &liveMaterialIds, &synchronization](const MaterialResource& material)
                {
                    if (material.id == InvalidResourceId)
                    {
                        return;
                    }

                    liveMaterialIds.insert(material.id);
                    const auto submittedRevision = synchronization.materialRevisions.find(material.id);
                    if (submittedRevision == synchronization.materialRevisions.end() ||
                        submittedRevision->second != material.revision)
                    {
                        update.materialUpdates.push_back(BuildRenderMaterialResourceUpdate(material));
                        synchronization.materialRevisions[material.id] = material.revision;
                    }
                });

            for (auto iter = synchronization.materialRevisions.begin();
                 iter != synchronization.materialRevisions.end();)
            {
                if (liveMaterialIds.find(iter->first) == liveMaterialIds.end())
                {
                    update.removedMaterials.push_back(iter->first);
                    iter = synchronization.materialRevisions.erase(iter);
                }
                else
                {
                    ++iter;
                }
            }

            synchronization.resourceChangeSerial = resourceChangeSerial;
            return update;
        }

        void ApplyRenderResourceRegistryUpdate(RenderSystemImpl& impl, const RenderResourceRegistryUpdate& update)
        {
            if (impl.rhi.device == nullptr)
            {
                TerminateRenderSystem("render-resource registry update failed", ErrorCode::InvalidState);
            }

            for (ResourceId resourceId : update.removedMeshes)
            {
                impl.rhi.sceneMeshes.erase(resourceId);
            }

            for (ResourceId resourceId : update.removedMaterials)
            {
                impl.rhi.sceneMaterials.erase(resourceId);
            }

            for (const RenderMeshResourceUpdate& mesh : update.meshUpdates)
            {
                if (mesh.resourceId == InvalidResourceId || mesh.vertices.empty())
                {
                    impl.rhi.sceneMeshes.erase(mesh.resourceId);
                    continue;
                }

                const auto existingMesh = impl.rhi.sceneMeshes.find(mesh.resourceId);
                if (existingMesh != impl.rhi.sceneMeshes.end() && existingMesh->second.revision == mesh.revision)
                {
                    continue;
                }

                rhi::RhiBufferDesc vertexBufferDesc = {};
                vertexBufferDesc.size = sizeof(RenderVertex) * mesh.vertices.size();
                vertexBufferDesc.usage = rhi::RhiBufferUsage::Vertex;
                vertexBufferDesc.initialData = mesh.vertices.data();
                vertexBufferDesc.debugName = "RenderSystemSceneMeshVertexBuffer";

                RenderSceneMeshResource renderMesh;
                renderMesh.vertexCount = static_cast<UInt32>(mesh.vertices.size());
                renderMesh.revision = mesh.revision;
                renderMesh.vertexBuffer = impl.rhi.device->CreateBuffer(vertexBufferDesc);
                if (renderMesh.vertexBuffer == nullptr)
                {
                    TerminateRenderSystem("render-resource registry mesh update failed", ErrorCode::PlatformError);
                }

                impl.rhi.sceneMeshes[mesh.resourceId] = std::move(renderMesh);
            }

            for (const RenderMaterialResourceUpdate& material : update.materialUpdates)
            {
                if (material.resourceId == InvalidResourceId)
                {
                    impl.rhi.sceneMaterials.erase(material.resourceId);
                    continue;
                }

                impl.rhi.sceneMaterials[material.resourceId] =
                    RenderSceneMaterialResource{material.baseColor, material.revision};
            }
        }

        [[nodiscard]] std::vector<SceneDrawCommand> BuildSceneDrawCommands(RenderSystemImpl& impl,
                                                                           const SceneRenderSnapshot& snapshot)
        {
            std::vector<SceneDrawCommand> drawCommands;
            if (!snapshot.hasMainCamera)
            {
                return drawCommands;
            }

            drawCommands.reserve(snapshot.drawItems.size());

            const Matrix44 viewProjection = snapshot.mainCamera.projectionMatrix * snapshot.mainCamera.viewMatrix;

            for (const SceneRenderDrawItem& drawItem : snapshot.drawItems)
            {
                const auto meshIter = impl.rhi.sceneMeshes.find(drawItem.mesh.GetId());
                if (meshIter == impl.rhi.sceneMeshes.end() || meshIter->second.vertexBuffer == nullptr ||
                    meshIter->second.vertexCount == 0)
                {
                    continue;
                }

                Vector3 baseColor = Vector3::One();
                const auto materialIter = impl.rhi.sceneMaterials.find(drawItem.material.GetId());
                if (materialIter != impl.rhi.sceneMaterials.end())
                {
                    baseColor = materialIter->second.baseColor;
                }

                drawCommands.push_back(SceneDrawCommand{
                    &meshIter->second, drawItem.worldMatrix, viewProjection * drawItem.worldMatrix, baseColor});
            }

            return drawCommands;
        }

        void RenderClearFrame(RenderSystemImpl& impl, RenderFrameContext& frameContext)
        {
            if (impl.rhi.device == nullptr || impl.rhi.mainSwapchain == nullptr)
            {
                TerminateRenderSystem("frame execution failed in RenderClearFrame", ErrorCode::InvalidState);
            }

            if (frameContext.commandList == nullptr)
            {
                TerminateRenderSystem("frame execution failed in RenderClearFrame", ErrorCode::InvalidState);
            }

            rhi::RhiRenderPassDesc renderPassDesc = {};
            renderPassDesc.colorLoadAction = rhi::RhiLoadAction::Clear;
            renderPassDesc.colorStoreAction = rhi::RhiStoreAction::Store;
            renderPassDesc.clearColor = {0.05f, 0.07f, 0.10f, 1.0f};

            const rhi::RhiExtent2D extent = impl.rhi.mainSwapchain->GetExtent();
            rhi::RhiCommandList& commandList = *frameContext.commandList;

            if (!commandList.Begin() || !commandList.BeginRenderPass(*impl.rhi.mainSwapchain, renderPassDesc))
            {
                TerminateRenderSystem("frame execution failed in RenderClearFrame command list begin",
                                      ErrorCode::PlatformError);
            }

            commandList.SetViewport(rhi::RhiViewport{
                0.0f, 0.0f, static_cast<Float32>(extent.width), static_cast<Float32>(extent.height), 0.0f, 1.0f});
            commandList.SetScissor(rhi::RhiScissorRect{0, 0, extent.width, extent.height});
            commandList.EndRenderPass();

            if (!commandList.End() || !impl.rhi.device->Submit(commandList) || !impl.rhi.mainSwapchain->Present())
            {
                TerminateRenderSystem("frame execution failed in RenderClearFrame submit", ErrorCode::PlatformError);
            }
        }

        void
        RenderSceneFrame(RenderSystemImpl& impl, RenderFrameContext& frameContext, const SceneRenderSnapshot& snapshot)
        {
            if (impl.rhi.device == nullptr || impl.rhi.mainSwapchain == nullptr)
            {
                TerminateRenderSystem("frame execution failed in RenderSceneFrame", ErrorCode::InvalidState);
            }

            if (impl.rhi.scenePipeline.pipelineState == nullptr || frameContext.commandList == nullptr)
            {
                TerminateRenderSystem("frame execution failed in RenderSceneFrame", ErrorCode::InvalidState);
            }

            std::vector<SceneDrawCommand> drawCommands = BuildSceneDrawCommands(impl, snapshot);

            frameContext.drawUniformBuffers.resize(drawCommands.size());
            for (SizeT index = 0; index < drawCommands.size(); ++index)
            {
                const SceneDrawCommand& drawCommand = drawCommands[index];
                const DrawUniformData uniforms = BuildSceneDrawUniformData(impl, snapshot, drawCommand);
                if (!EnsureReusableBuffer(impl,
                                          frameContext.drawUniformBuffers[index],
                                          rhi::RhiBufferUsage::Uniform,
                                          &uniforms,
                                          sizeof(DrawUniformData),
                                          "RenderSystemSceneDrawUniformBuffer"))
                {
                    TerminateRenderSystem("frame execution failed in RenderSceneFrame uniform buffer",
                                          ErrorCode::PlatformError);
                }
            }

            rhi::RhiRenderPassDesc renderPassDesc = {};
            renderPassDesc.colorLoadAction = rhi::RhiLoadAction::Clear;
            renderPassDesc.colorStoreAction = rhi::RhiStoreAction::Store;
            if (snapshot.hasMainCamera)
            {
                const Vector4 clearColor = snapshot.mainCamera.clearColor;
                renderPassDesc.clearColor = {
                    clearColor.GetX(), clearColor.GetY(), clearColor.GetZ(), clearColor.GetW()};
            }
            else
            {
                renderPassDesc.clearColor = {0.05f, 0.07f, 0.10f, 1.0f};
            }

            const rhi::RhiExtent2D extent = impl.rhi.mainSwapchain->GetExtent();
            rhi::RhiCommandList& commandList = *frameContext.commandList;

            if (!commandList.Begin() || !commandList.BeginRenderPass(*impl.rhi.mainSwapchain, renderPassDesc))
            {
                TerminateRenderSystem("frame execution failed in RenderSceneFrame command list begin",
                                      ErrorCode::PlatformError);
            }

            commandList.SetViewport(rhi::RhiViewport{
                0.0f, 0.0f, static_cast<Float32>(extent.width), static_cast<Float32>(extent.height), 0.0f, 1.0f});
            commandList.SetScissor(rhi::RhiScissorRect{0, 0, extent.width, extent.height});
            commandList.SetPipeline(*impl.rhi.scenePipeline.pipelineState);
            for (SizeT index = 0; index < drawCommands.size(); ++index)
            {
                const SceneDrawCommand& drawCommand = drawCommands[index];
                commandList.SetVertexBuffer(0, *drawCommand.mesh->vertexBuffer, sizeof(RenderVertex), 0);
                commandList.SetUniformBuffer(
                    rhi::RhiShaderStage::Vertex,
                    0,
                    *frameContext.drawUniformBuffers[index].buffer,
                    0,
                    sizeof(DrawUniformData));
                commandList.Draw(drawCommand.mesh->vertexCount, 0);
            }
            commandList.EndRenderPass();

            if (!commandList.End() || !impl.rhi.device->Submit(commandList) || !impl.rhi.mainSwapchain->Present())
            {
                TerminateRenderSystem("frame execution failed in RenderSceneFrame submit", ErrorCode::PlatformError);
            }
        }

        [[nodiscard]] bool MakeEditorUiScissor(const EditorUiFrameData& frameData,
                                               const Float32* clipRect,
                                               const rhi::RhiExtent2D& extent,
                                               rhi::RhiScissorRect& outScissor) noexcept
        {
            const Float32 scaleX = frameData.framebufferScale[0] == 0.0f ? 1.0f : frameData.framebufferScale[0];
            const Float32 scaleY = frameData.framebufferScale[1] == 0.0f ? 1.0f : frameData.framebufferScale[1];

            Float32 clipMinX = (clipRect[0] - frameData.displayPos[0]) * scaleX;
            Float32 clipMinY = (clipRect[1] - frameData.displayPos[1]) * scaleY;
            Float32 clipMaxX = (clipRect[2] - frameData.displayPos[0]) * scaleX;
            Float32 clipMaxY = (clipRect[3] - frameData.displayPos[1]) * scaleY;

            clipMinX = std::clamp(clipMinX, 0.0f, static_cast<Float32>(extent.width));
            clipMinY = std::clamp(clipMinY, 0.0f, static_cast<Float32>(extent.height));
            clipMaxX = std::clamp(clipMaxX, 0.0f, static_cast<Float32>(extent.width));
            clipMaxY = std::clamp(clipMaxY, 0.0f, static_cast<Float32>(extent.height));

            if (clipMaxX <= clipMinX || clipMaxY <= clipMinY)
            {
                return false;
            }

            const Int32 x = static_cast<Int32>(std::floor(clipMinX));
            const Int32 y = static_cast<Int32>(std::floor(clipMinY));
            const UInt32 width = static_cast<UInt32>(std::ceil(clipMaxX) - std::floor(clipMinX));
            const UInt32 height = static_cast<UInt32>(std::ceil(clipMaxY) - std::floor(clipMinY));
            outScissor = rhi::RhiScissorRect{x, y, width, height};
            return width > 0 && height > 0;
        }

        void EnsureEditorUiFontTexture(RenderSystemImpl& impl, const EditorUiFrameData& frameData)
        {
            if (impl.rhi.editorUiPipeline.fontTexture != nullptr)
            {
                return;
            }

            if (!frameData.fontAtlas.IsValid())
            {
                TerminateRenderSystem("Editor UI frame is missing the initial font atlas", ErrorCode::InvalidState);
            }

            impl.rhi.editorUiPipeline.fontTexture = CreateEditorUiFontTexture(impl, frameData.fontAtlas);
            if (impl.rhi.editorUiPipeline.fontTexture == nullptr)
            {
                TerminateRenderSystem("Editor UI font texture creation failed", ErrorCode::PlatformError);
            }
        }

        [[nodiscard]] const rhi::RhiTexture* ResolveEditorUiTexture(RenderSystemImpl& impl, UInt64 textureId) noexcept
        {
            if (textureId <= 1)
            {
                return impl.rhi.editorUiPipeline.fontTexture.get();
            }

            const auto iter = impl.rhi.editorViewportTextures.find(textureId);
            if (iter == impl.rhi.editorViewportTextures.end())
            {
                return nullptr;
            }

            return iter->second.colorTexture.get();
        }

        [[nodiscard]] EditorViewportTexture& EnsureEditorViewportTexture(RenderSystemImpl& impl,
                                                                         UInt64 textureId,
                                                                         UInt32 width,
                                                                         UInt32 height)
        {
            VE_ASSERT_MESSAGE(impl.rhi.device != nullptr,
                              "EnsureEditorViewportTexture requires an initialized RHI device.");
            VE_ASSERT_MESSAGE(impl.rhi.mainSwapchain != nullptr,
                              "EnsureEditorViewportTexture requires an initialized main swapchain.");

            const rhi::RhiFormat format = impl.rhi.mainSwapchain->GetColorFormat();
            EditorViewportTexture& viewportTexture = impl.rhi.editorViewportTextures[textureId];
            if (viewportTexture.commandList == nullptr)
            {
                viewportTexture.commandList = impl.rhi.device->CreateCommandList();
                if (viewportTexture.commandList == nullptr)
                {
                    TerminateRenderSystem("Editor viewport command list creation failed", ErrorCode::PlatformError);
                }
            }

            if (viewportTexture.colorTexture != nullptr && viewportTexture.width == width &&
                viewportTexture.height == height && viewportTexture.format == format)
            {
                return viewportTexture;
            }

            rhi::RhiTextureDesc textureDesc = {};
            textureDesc.dimension = rhi::RhiTextureDimension::Texture2D;
            textureDesc.width = width;
            textureDesc.height = height;
            textureDesc.format = format;
            textureDesc.usage =
                CombineTextureUsage(rhi::RhiTextureUsage::Sampled, rhi::RhiTextureUsage::RenderTarget);
            textureDesc.debugName = "RenderSystemEditorViewportTexture";

            viewportTexture.colorTexture = impl.rhi.device->CreateTexture(textureDesc);
            if (viewportTexture.colorTexture == nullptr)
            {
                TerminateRenderSystem("Editor viewport texture creation failed", ErrorCode::PlatformError);
            }

            viewportTexture.width = width;
            viewportTexture.height = height;
            viewportTexture.format = format;
            return viewportTexture;
        }

        void RenderSceneViewport(RenderSystemImpl& impl, const EditorViewportRenderRequest& request)
        {
            if (impl.rhi.device == nullptr || impl.rhi.mainSwapchain == nullptr)
            {
                TerminateRenderSystem("viewport execution failed in RenderSceneViewport", ErrorCode::InvalidState);
            }

            if (impl.rhi.scenePipeline.pipelineState == nullptr)
            {
                TerminateRenderSystem("viewport execution failed in RenderSceneViewport", ErrorCode::InvalidState);
            }

            if (request.textureId == 0 || request.width == 0 || request.height == 0)
            {
                return;
            }

            EditorViewportTexture& viewportTexture =
                EnsureEditorViewportTexture(impl, request.textureId, request.width, request.height);

            std::vector<SceneDrawCommand> drawCommands = BuildSceneDrawCommands(impl, request.snapshot);

            viewportTexture.drawUniformBuffers.resize(drawCommands.size());
            for (SizeT index = 0; index < drawCommands.size(); ++index)
            {
                const SceneDrawCommand& drawCommand = drawCommands[index];
                const DrawUniformData uniforms = BuildSceneDrawUniformData(impl, request.snapshot, drawCommand);
                if (!EnsureReusableBuffer(impl,
                                          viewportTexture.drawUniformBuffers[index],
                                          rhi::RhiBufferUsage::Uniform,
                                          &uniforms,
                                          sizeof(DrawUniformData),
                                          "RenderSystemEditorViewportDrawUniformBuffer"))
                {
                    TerminateRenderSystem("viewport execution failed in RenderSceneViewport uniform buffer",
                                          ErrorCode::PlatformError);
                }
            }

            rhi::RhiRenderPassDesc renderPassDesc = {};
            renderPassDesc.colorLoadAction = rhi::RhiLoadAction::Clear;
            renderPassDesc.colorStoreAction = rhi::RhiStoreAction::Store;
            if (request.snapshot.hasMainCamera)
            {
                const Vector4 clearColor = request.snapshot.mainCamera.clearColor;
                renderPassDesc.clearColor = {
                    clearColor.GetX(), clearColor.GetY(), clearColor.GetZ(), clearColor.GetW()};
            }
            else
            {
                renderPassDesc.clearColor = {0.05f, 0.07f, 0.10f, 1.0f};
            }

            rhi::RhiCommandList& commandList = *viewportTexture.commandList;
            if (!commandList.Begin() || !commandList.BeginRenderPass(*viewportTexture.colorTexture, renderPassDesc))
            {
                TerminateRenderSystem("Editor viewport command list begin failed", ErrorCode::PlatformError);
            }

            commandList.SetViewport(rhi::RhiViewport{0.0f,
                                                     0.0f,
                                                     static_cast<Float32>(request.width),
                                                     static_cast<Float32>(request.height),
                                                     0.0f,
                                                     1.0f});
            commandList.SetScissor(rhi::RhiScissorRect{0, 0, request.width, request.height});
            commandList.SetPipeline(*impl.rhi.scenePipeline.pipelineState);
            for (SizeT index = 0; index < drawCommands.size(); ++index)
            {
                const SceneDrawCommand& drawCommand = drawCommands[index];
                commandList.SetVertexBuffer(0, *drawCommand.mesh->vertexBuffer, sizeof(RenderVertex), 0);
                commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex,
                                             0,
                                             *viewportTexture.drawUniformBuffers[index].buffer,
                                             0,
                                             sizeof(DrawUniformData));
                commandList.Draw(drawCommand.mesh->vertexCount, 0);
            }
            commandList.EndRenderPass();

            if (!commandList.End() || !impl.rhi.device->Submit(commandList))
            {
                TerminateRenderSystem("Editor viewport frame submit failed", ErrorCode::PlatformError);
            }
        }

        void RenderEditorViewportFrame(RenderSystemImpl& impl,
                                       const EditorViewportFrameData& frameData,
                                       const RenderResourceRegistryUpdate& resourceUpdate)
        {
            if (resourceUpdate.IsEmpty() && frameData.IsEmpty())
            {
                return;
            }

            if (!resourceUpdate.IsEmpty())
            {
                ApplyRenderResourceRegistryUpdate(impl, resourceUpdate);
            }

            for (const EditorViewportRenderRequest& request : frameData.viewports)
            {
                RenderSceneViewport(impl, request);
            }
        }

        void RenderEditorUiFrame(RenderSystemImpl& impl, const EditorUiFrameData& frameData)
        {
            if (impl.rhi.device == nullptr || impl.rhi.mainSwapchain == nullptr)
            {
                TerminateRenderSystem("frame execution failed in RenderEditorUiFrame", ErrorCode::InvalidState);
            }

            if (impl.rhi.editorUiPipeline.pipelineState == nullptr)
            {
                TerminateRenderSystem("frame execution failed in RenderEditorUiFrame", ErrorCode::InvalidState);
            }

            if (!frameData.HasDrawableArea())
            {
                return;
            }

            EnsureEditorUiFontTexture(impl, frameData);

            struct EditorUiBufferedDrawCommand
            {
                UInt32 elementCount = 0;
                UInt32 indexOffset = 0;
                UInt32 vertexOffset = 0;
                Float32 clipRect[4] = {};
                UInt64 textureId = 0;
            };

            std::vector<EditorUiVertex> vertices;
            std::vector<UInt32> indices;
            std::vector<EditorUiBufferedDrawCommand> commands;

            for (const EditorUiDrawList& drawList : frameData.drawLists)
            {
                if (drawList.vertices.empty() || drawList.indices.empty() || drawList.commands.empty())
                {
                    continue;
                }

                const UInt32 baseVertex = static_cast<UInt32>(vertices.size());
                const UInt32 baseIndex = static_cast<UInt32>(indices.size());
                vertices.insert(vertices.end(), drawList.vertices.begin(), drawList.vertices.end());
                indices.insert(indices.end(), drawList.indices.begin(), drawList.indices.end());

                for (const EditorUiDrawCommand& drawCommand : drawList.commands)
                {
                    EditorUiBufferedDrawCommand command;
                    command.elementCount = drawCommand.elementCount;
                    command.indexOffset = baseIndex + drawCommand.indexOffset;
                    command.vertexOffset = baseVertex + drawCommand.vertexOffset;
                    command.clipRect[0] = drawCommand.clipRect[0];
                    command.clipRect[1] = drawCommand.clipRect[1];
                    command.clipRect[2] = drawCommand.clipRect[2];
                    command.clipRect[3] = drawCommand.clipRect[3];
                    command.textureId = drawCommand.textureId;
                    commands.push_back(command);
                }
            }

            if (!vertices.empty() &&
                !EnsureReusableBuffer(impl,
                                      impl.rhi.editorUiPipeline.vertexBuffer,
                                      rhi::RhiBufferUsage::Vertex,
                                      vertices.data(),
                                      sizeof(EditorUiVertex) * vertices.size(),
                                      "RenderSystemEditorUiVertexBuffer"))
            {
                TerminateRenderSystem("Editor UI vertex buffer update failed", ErrorCode::PlatformError);
            }

            if (!indices.empty() &&
                !EnsureReusableBuffer(impl,
                                      impl.rhi.editorUiPipeline.indexBuffer,
                                      rhi::RhiBufferUsage::Index,
                                      indices.data(),
                                      sizeof(UInt32) * indices.size(),
                                      "RenderSystemEditorUiIndexBuffer"))
            {
                TerminateRenderSystem("Editor UI index buffer update failed", ErrorCode::PlatformError);
            }

            const EditorUiUniformData uniforms = BuildEditorUiUniformData(impl, frameData);
            if (!EnsureReusableBuffer(impl,
                                      impl.rhi.editorUiPipeline.uniformBuffer,
                                      rhi::RhiBufferUsage::Uniform,
                                      &uniforms,
                                      sizeof(EditorUiUniformData),
                                      "RenderSystemEditorUiUniformBuffer"))
            {
                TerminateRenderSystem("Editor UI uniform buffer update failed", ErrorCode::PlatformError);
            }

            if (impl.rhi.editorUiPipeline.commandList == nullptr)
            {
                TerminateRenderSystem("Editor UI command list is missing", ErrorCode::InvalidState);
            }

            rhi::RhiCommandList& commandList = *impl.rhi.editorUiPipeline.commandList;
            rhi::RhiRenderPassDesc renderPassDesc = {};
            renderPassDesc.colorLoadAction = rhi::RhiLoadAction::Clear;
            renderPassDesc.colorStoreAction = rhi::RhiStoreAction::Store;
            renderPassDesc.clearColor = {0.055f, 0.06f, 0.065f, 1.0f};

            const rhi::RhiExtent2D extent = impl.rhi.mainSwapchain->GetExtent();
            if (!commandList.Begin() || !commandList.BeginRenderPass(*impl.rhi.mainSwapchain, renderPassDesc))
            {
                TerminateRenderSystem("Editor UI command list begin failed", ErrorCode::PlatformError);
            }

            commandList.SetViewport(rhi::RhiViewport{
                0.0f, 0.0f, static_cast<Float32>(extent.width), static_cast<Float32>(extent.height), 0.0f, 1.0f});
            commandList.SetPipeline(*impl.rhi.editorUiPipeline.pipelineState);
            commandList.SetUniformBuffer(
                rhi::RhiShaderStage::Vertex,
                0,
                *impl.rhi.editorUiPipeline.uniformBuffer.buffer,
                0,
                sizeof(EditorUiUniformData));

            if (!commands.empty() && impl.rhi.editorUiPipeline.vertexBuffer.buffer != nullptr &&
                impl.rhi.editorUiPipeline.indexBuffer.buffer != nullptr)
            {
                commandList.SetVertexBuffer(
                    0, *impl.rhi.editorUiPipeline.vertexBuffer.buffer, sizeof(EditorUiVertex), 0);
                commandList.SetIndexBuffer(
                    *impl.rhi.editorUiPipeline.indexBuffer.buffer, rhi::RhiIndexFormat::UInt32, 0);

                for (const EditorUiBufferedDrawCommand& drawCommand : commands)
                {
                    if (drawCommand.elementCount == 0)
                    {
                        continue;
                    }

                    rhi::RhiScissorRect scissor = {};
                    if (!MakeEditorUiScissor(frameData, drawCommand.clipRect, extent, scissor))
                    {
                        continue;
                    }

                    commandList.SetScissor(scissor);
                    const rhi::RhiTexture* texture = ResolveEditorUiTexture(impl, drawCommand.textureId);
                    if (texture == nullptr)
                    {
                        continue;
                    }

                    commandList.SetTexture(rhi::RhiShaderStage::Fragment, 0, *texture);
                    commandList.DrawIndexed(drawCommand.elementCount,
                                            drawCommand.indexOffset,
                                            static_cast<int32_t>(drawCommand.vertexOffset));
                }
            }

            commandList.EndRenderPass();

            if (!commandList.End() || !impl.rhi.device->Submit(commandList) || !impl.rhi.mainSwapchain->Present())
            {
                TerminateRenderSystem("Editor UI frame submit failed", ErrorCode::PlatformError);
            }
        }

        [[nodiscard]] UInt32 GetRenderFrameSlotIndex(UInt64 frameId, UInt32 maxFramesInFlight) noexcept
        {
            VE_ASSERT_MESSAGE(frameId > 0, "Render frame slot index requires a non-zero frame id.");
            VE_ASSERT_MESSAGE(maxFramesInFlight > 0, "Render frame slot index requires at least one frame slot.");
            return static_cast<UInt32>((frameId - 1) % static_cast<UInt64>(maxFramesInFlight));
        }

        [[nodiscard]] RenderFrameContext* FindRenderFrameContext(RenderSystemImpl& impl,
                                                                 const RenderFrameToken& token) noexcept
        {
            if (token.frameId == 0 || impl.frameRing.contexts.empty())
            {
                return nullptr;
            }

            const UInt32 slotIndex = GetRenderFrameSlotIndex(token.frameId, impl.frameRing.maxFramesInFlight);
            if (slotIndex >= impl.frameRing.contexts.size())
            {
                return nullptr;
            }

            RenderFrameContext& frameContext = impl.frameRing.contexts[slotIndex];
            if (frameContext.frameId != token.frameId)
            {
                return nullptr;
            }

            return &frameContext;
        }

        void AcquireRenderFrameSlot(RenderSystemImpl& impl, RenderFrameToken& outToken)
        {
            if (!impl.commands.acceptingCommands.load(std::memory_order_acquire))
            {
                TerminateRenderSystem("submission failed in RenderFrame acquire", ErrorCode::InvalidState);
            }

            ++impl.frameRing.nextRenderFrameId;
            const UInt32 slotIndex =
                GetRenderFrameSlotIndex(impl.frameRing.nextRenderFrameId, impl.frameRing.maxFramesInFlight);
            RenderFrameContext& frameContext = impl.frameRing.contexts[slotIndex];
            VE_ASSERT_MESSAGE(frameContext.state == RenderFrameSlotState::Free,
                              "Render frame ring slot should be free.");

            frameContext.frameId = impl.frameRing.nextRenderFrameId;
            frameContext.state = RenderFrameSlotState::Submitted;
            frameContext.lastError = ErrorCode::None;
            outToken.frameId = frameContext.frameId;
        }

        void MarkRenderFrameSlotRendering(RenderSystemImpl& impl, const RenderFrameToken& token) noexcept
        {
            RenderFrameContext* frameContext = FindRenderFrameContext(impl, token);
            VE_ASSERT_MESSAGE(frameContext != nullptr, "Render frame token must refer to a live frame context.");
            if (frameContext != nullptr)
            {
                frameContext->state = RenderFrameSlotState::Rendering;
            }
        }

        void ReleaseRenderFrameSlot(RenderSystemImpl& impl, const RenderFrameToken& token, ErrorCode result) noexcept
        {
            RenderFrameContext* frameContext = FindRenderFrameContext(impl, token);
            VE_ASSERT_MESSAGE(frameContext != nullptr, "Render frame slot release requires a live frame context.");
            if (frameContext != nullptr)
            {
                frameContext->lastError = result;
                frameContext->frameId = 0;
                frameContext->state = RenderFrameSlotState::Free;
            }
        }

        void ExecuteRenderFrame(RenderSystemImpl& impl, const RenderFrameToken& token) noexcept
        {
            MarkRenderFrameSlotRendering(impl, token);

            RenderFrameContext* frameContext = nullptr;
            frameContext = FindRenderFrameContext(impl, token);

            if (frameContext == nullptr)
            {
                TerminateRenderSystem("frame execution failed in RenderFrame", ErrorCode::InvalidState);
            }

            RenderClearFrame(impl, *frameContext);
        }

        void ExecuteSceneRenderFrame(RenderSystemImpl& impl,
                                     const RenderFrameToken& token,
                                     const SceneRenderSnapshot& snapshot) noexcept
        {
            MarkRenderFrameSlotRendering(impl, token);

            RenderFrameContext* frameContext = FindRenderFrameContext(impl, token);
            if (frameContext == nullptr)
            {
                TerminateRenderSystem("frame execution failed in SubmitFrame", ErrorCode::InvalidState);
            }

            RenderSceneFrame(impl, *frameContext, snapshot);
        }

        void ExecuteCommand(RenderThreadContext& context, RenderCommand& command) noexcept
        {
            try
            {
                command.function(context);
            }
            catch (...)
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped a RenderSystem command.");
            }
        }

        void RenderThreadLoop(RenderSystemImpl& impl)
        {
            const ThreadId renderThreadId = GetCurrentThreadId();
            impl.thread.renderThreadIdValue.store(renderThreadId.value, std::memory_order_release);
            RenderThreadContext context(renderThreadId);

            for (;;)
            {
                while (std::optional<RenderCommand> command = impl.commands.queue.TryPop())
                {
                    ExecuteCommand(context, *command);
                }

                if (impl.thread.stopRequested.load(std::memory_order_acquire))
                {
                    if (!impl.commands.queue.IsEmptyForConsumer())
                    {
                        continue;
                    }

                    break;
                }

                impl.commands.semaphore.Acquire();
            }

            while (std::optional<RenderCommand> command = impl.commands.queue.TryPop())
            {
                ExecuteCommand(context, *command);
            }
        }

        void DestroyRhiStateOnRenderThread(RenderSystemImpl& impl)
        {
            if (impl.rhi.device != nullptr)
            {
                impl.rhi.device->WaitIdle();
            }

            DestroyEditorUiPipelineResources(impl);
            DestroyScenePipelineResources(impl);
            impl.rhi.mainSwapchain.reset();

            if (impl.rhi.device != nullptr)
            {
                impl.rhi.device.reset();
            }

            impl.rhi.backendValue.store(-1, std::memory_order_release);
        }

        void EnqueueInternalCommand(RenderSystemImpl& impl, RenderCommand command) noexcept
        {
            ErrorCode pushResult = impl.commands.queue.Push(std::move(command));
            VE_ASSERT_MESSAGE(pushResult == ErrorCode::None,
                              "RenderSystem failed to enqueue an internal render command.");
            impl.commands.semaphore.Release();
        }

        void StopAndJoinRenderThread(RenderSystemImpl& impl) noexcept
        {
            impl.commands.acceptingCommands.store(false, std::memory_order_release);

            while (impl.commands.activeSubmitCount.load(std::memory_order_acquire) != 0)
            {
                YieldThread();
            }

            auto rhiDestroyed = std::make_shared<ManualResetEvent>(false);
            EnqueueInternalCommand(impl,
                                   RenderCommand{"RenderSystemDestroyRhiState",
                                                 [&impl, rhiDestroyed](RenderThreadContext&)
                                                 {
                                                     DestroyRhiStateOnRenderThread(impl);
                                                     rhiDestroyed->Set();
                                                 }});
            rhiDestroyed->Wait();

            impl.thread.stopRequested.store(true, std::memory_order_release);
            impl.commands.semaphore.Release();

            if (impl.thread.ownedThread.IsJoinable())
            {
                const bool joined = impl.thread.ownedThread.Join();
                VE_ASSERT_MESSAGE(joined, "RenderSystem failed to join its Render Thread during shutdown.");
            }

            impl.commands.queue.ClearForConsumer();
            impl.thread.renderThreadIdValue.store(0, std::memory_order_release);
            impl.thread.stopRequested.store(false, std::memory_order_release);
            impl.thread.initialized.store(false, std::memory_order_release);
        }
    } // namespace

    RenderThreadContext::RenderThreadContext(ThreadId renderThreadId) noexcept
        : renderThreadId_(renderThreadId)
    {
    }

    ThreadId RenderThreadContext::GetRenderThreadId() const noexcept
    {
        return renderThreadId_;
    }

    RenderCommandFence::RenderCommandFence() = default;

    RenderCommandFence::~RenderCommandFence() = default;

    bool RenderCommandFence::IsComplete() const noexcept
    {
        return completed_ == nullptr || completed_->TryWait();
    }

    void RenderCommandFence::Wait() noexcept
    {
        if (completed_ != nullptr)
        {
            completed_->Wait();
        }
    }

    void RenderCommandFence::SetCompletionEvent(std::shared_ptr<ManualResetEvent> completed) noexcept
    {
        completed_ = std::move(completed);
    }

    RenderSystem::RenderSystem()
        : impl_(std::make_unique<RenderSystemImpl>())
    {
    }

    RenderSystem::~RenderSystem()
    {
        Shutdown();
    }

    void RenderSystem::Initialize(const RenderSystemDesc& desc)
    {
        if (impl_->thread.initialized.load(std::memory_order_acquire))
        {
            TerminateRenderSystem("initialization failed: RenderSystem is already initialized",
                                  ErrorCode::InvalidState);
        }

        if (desc.maxFramesInFlight == 0)
        {
            TerminateRenderSystem("initialization failed: maxFramesInFlight must be greater than zero",
                                  ErrorCode::InvalidArgument);
        }

        impl_->thread.stopRequested.store(false, std::memory_order_release);
        impl_->commands.acceptingCommands.store(true, std::memory_order_release);
        impl_->thread.renderThreadIdValue.store(0, std::memory_order_release);
        impl_->gameThreadBinding.gameThreadIdValue.store(0, std::memory_order_release);
        impl_->rhi.backendValue.store(-1, std::memory_order_release);
        ClearResourceSynchronizationState(*impl_);
        impl_->frameRing.maxFramesInFlight = desc.maxFramesInFlight;

        impl_->frameRing.nextRenderFrameId = 0;
        impl_->frameRing.contexts.clear();
        try
        {
            impl_->frameRing.contexts.resize(desc.maxFramesInFlight);
        }
        catch (const std::bad_alloc&)
        {
            TerminateRenderSystem("initialization failed: failed to allocate render frame contexts",
                                  ErrorCode::OutOfMemory);
        }

        for (RenderFrameContext& frameContext : impl_->frameRing.contexts)
        {
            frameContext.state = RenderFrameSlotState::Free;
            frameContext.lastError = ErrorCode::None;
        }

        ErrorCode startResult = impl_->thread.ownedThread.Start(
            desc.threadName.empty() ? ThreadDesc{"VEngineRenderThread"} : ThreadDesc{desc.threadName},
            [this]() { RenderThreadLoop(*impl_); });

        if (startResult != ErrorCode::None)
        {
            TerminateRenderSystem("initialization failed: failed to start Render Thread", startResult);
        }

        impl_->thread.initialized.store(true, std::memory_order_release);
    }

    void RenderSystem::Shutdown() noexcept
    {
        if (!impl_->thread.initialized.load(std::memory_order_acquire))
        {
            return;
        }

        StopAndJoinRenderThread(*impl_);
        ClearResourceSynchronizationState(*impl_);
    }

    void RenderSystem::BindGameThread(ThreadId gameThreadId) noexcept
    {
        VE_ASSERT_MESSAGE(gameThreadId.IsValid(), "RenderSystem::BindGameThread requires a valid Game Thread id.");
        impl_->gameThreadBinding.gameThreadIdValue.store(gameThreadId.value, std::memory_order_release);
    }

    void RenderSystem::ClearGameThreadBinding() noexcept
    {
        impl_->gameThreadBinding.gameThreadIdValue.store(0, std::memory_order_release);
    }

    void RenderSystem::BeginFrameEndFence(RenderCommandFence& fence)
    {
        ValidateGameThreadAccess(*impl_, "RenderSystem::BeginFrameEndFence must be called on the Game Thread.");

        auto completed = std::make_shared<ManualResetEvent>(false);
        fence.SetCompletionEvent(completed);
        SubmitFunction("RenderSystemFrameEndFence", [completed](RenderThreadContext&) { completed->Set(); });
    }

    bool RenderSystem::IsInitialized() const noexcept
    {
        return impl_->thread.initialized.load(std::memory_order_acquire);
    }

    ThreadId RenderSystem::GetRenderThreadId() const noexcept
    {
        return ThreadId{impl_->thread.renderThreadIdValue.load(std::memory_order_acquire)};
    }

    ErrorCode RenderSystem::InitializeDevice(const RenderDeviceDesc& desc)
    {
        return ExecuteSynchronous("RenderSystemInitializeDevice",
                                  [this, desc](RenderThreadContext&)
                                  {
                                      if (impl_->rhi.device != nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      std::unique_ptr<rhi::RhiDevice> device = CreateRhiDevice(desc);
                                      if (device == nullptr)
                                      {
                                          return ErrorCode::Unsupported;
                                      }

                                      impl_->rhi.device = std::move(device);
                                      impl_->rhi.backendValue.store(static_cast<int>(desc.backend),
                                                                    std::memory_order_release);
                                      VE_LOG_INFO("RenderSystem initialized RHI backend: {}", ToString(desc.backend));
                                      return ErrorCode::None;
                                  });
    }

    void RenderSystem::ShutdownDevice() noexcept
    {
        if (!impl_->commands.acceptingCommands.load(std::memory_order_acquire))
        {
            return;
        }

        ErrorCode result = ExecuteSynchronous("RenderSystemShutdownDevice",
                                              [this](RenderThreadContext&)
                                              {
                                                  DestroyRhiStateOnRenderThread(*impl_);
                                                  return ErrorCode::None;
                                              });

        VE_ASSERT_MESSAGE(result == ErrorCode::None, "RenderSystem failed to shut down its RHI device.");
        ClearResourceSynchronizationState(*impl_);
    }

    bool RenderSystem::HasDevice() const noexcept
    {
        return impl_->rhi.backendValue.load(std::memory_order_acquire) >= 0;
    }

    RenderBackend RenderSystem::GetDeviceBackend() const noexcept
    {
        const int backendValue = impl_->rhi.backendValue.load(std::memory_order_acquire);
        VE_ASSERT_MESSAGE(backendValue >= 0, "RenderSystem::GetDeviceBackend requires an initialized RHI device.");
        return static_cast<RenderBackend>(backendValue);
    }

    ErrorCode RenderSystem::CreateMainSwapchain(const RenderSurfaceDesc& desc)
    {
        ErrorCode validateResult = ValidateSurfaceDesc(desc);
        if (validateResult != ErrorCode::None)
        {
            return validateResult;
        }

        return ExecuteSynchronous("RenderSystemCreateMainSwapchain",
                                  [this, desc](RenderThreadContext&)
                                  {
                                      if (impl_->rhi.device == nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      if (impl_->rhi.mainSwapchain != nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      std::unique_ptr<rhi::RhiSwapchain> swapchain =
                                          impl_->rhi.device->CreateSwapchain(ToRhiSwapchainDesc(desc));
                                      if (swapchain == nullptr)
                                      {
                                          return ErrorCode::PlatformError;
                                      }

                                      impl_->rhi.mainSwapchain = std::move(swapchain);
                                      ErrorCode pipelineResult = CreateScenePipelineResources(*impl_);
                                      if (pipelineResult != ErrorCode::None)
                                      {
                                          impl_->rhi.mainSwapchain.reset();
                                          return pipelineResult;
                                      }

                                      pipelineResult = CreateEditorUiPipelineResources(*impl_);
                                      if (pipelineResult != ErrorCode::None)
                                      {
                                          DestroyScenePipelineResources(*impl_);
                                          impl_->rhi.mainSwapchain.reset();
                                          return pipelineResult;
                                      }

                                      return ErrorCode::None;
                                  });
    }

    void RenderSystem::DestroyMainSwapchain() noexcept
    {
        if (!impl_->commands.acceptingCommands.load(std::memory_order_acquire))
        {
            return;
        }

        ErrorCode result = ExecuteSynchronous("RenderSystemDestroyMainSwapchain",
                                              [this](RenderThreadContext&)
                                              {
                                                  if (impl_->rhi.device != nullptr)
                                                  {
                                                      impl_->rhi.device->WaitIdle();
                                                  }

                                                  DestroyEditorUiPipelineResources(*impl_);
                                                  DestroyScenePipelineResources(*impl_);
                                                  impl_->rhi.mainSwapchain.reset();
                                                  return ErrorCode::None;
                                              });

        VE_ASSERT_MESSAGE(result == ErrorCode::None, "RenderSystem failed to destroy its main swapchain.");
        ClearResourceSynchronizationState(*impl_);
    }

    void RenderSystem::RenderFrame()
    {
        ValidateGameThreadAccess(*impl_, "RenderSystem::RenderFrame must be called on the Game Thread.");

        RenderFrameToken token;
        AcquireRenderFrameSlot(*impl_, token);

        SubmitFunction("RenderSystemRenderFrame",
                       [this, token](RenderThreadContext&)
                       {
                           ExecuteRenderFrame(*impl_, token);
                           ReleaseRenderFrameSlot(*impl_, token, ErrorCode::None);
                       });
    }

    void RenderSystem::SubmitFrame(SceneRenderSnapshot snapshot)
    {
        ValidateGameThreadAccess(*impl_, "RenderSystem::SubmitFrame must be called on the Game Thread.");

        RenderFrameToken token;
        AcquireRenderFrameSlot(*impl_, token);

        SubmitFunction("RenderSystemSubmitFrame",
                       [this, token, snapshot = std::move(snapshot)](RenderThreadContext&)
                       {
                           ExecuteSceneRenderFrame(*impl_, token, snapshot);
                           ReleaseRenderFrameSlot(*impl_, token, ErrorCode::None);
                       });
    }

    void RenderSystem::SubmitEditorUiFrame(EditorUiFrameData frameData)
    {
        auto completed = std::make_shared<ManualResetEvent>(false);
        SubmitFunction("RenderSystemSubmitEditorUiFrame",
                       [this, frameData = std::move(frameData), completed](RenderThreadContext&)
                       {
                           RenderEditorUiFrame(*impl_, frameData);
                           completed->Set();
                       });

        completed->Wait();
    }

    void RenderSystem::SubmitEditorViewportFrame(EditorViewportFrameData frameData,
                                                 const ResourceManager& resourceManager)
    {
        RenderResourceRegistryUpdate resourceUpdate = BuildRenderResourceRegistryUpdate(*impl_, resourceManager);
        if (frameData.IsEmpty() && resourceUpdate.IsEmpty())
        {
            return;
        }

        SubmitFunction("RenderSystemSubmitEditorViewportFrame",
                       [this, frameData = std::move(frameData), resourceUpdate = std::move(resourceUpdate)](
                           RenderThreadContext&)
                       {
                           RenderEditorViewportFrame(*impl_, frameData, resourceUpdate);
                       });
    }

    void RenderSystem::SynchronizeRenderResources(const ResourceManager& resourceManager)
    {
        ValidateGameThreadAccess(*impl_,
                                 "RenderSystem::SynchronizeRenderResources must be called on the Game Thread.");

        RenderResourceRegistryUpdate update = BuildRenderResourceRegistryUpdate(*impl_, resourceManager);
        if (update.IsEmpty())
        {
            return;
        }

        SubmitFunction("RenderSystemSynchronizeRenderResources",
                       [this, update = std::move(update)](RenderThreadContext&)
                       { ApplyRenderResourceRegistryUpdate(*impl_, update); });
    }

    UInt32 RenderSystem::GetMaxRenderFramesInFlight() const noexcept
    {
        return impl_->frameRing.maxFramesInFlight;
    }

    SizeT RenderSystem::GetRenderFramesInFlight() const noexcept
    {
        return 0;
    }

    void RenderSystem::Submit(RenderCommand command)
    {
        ValidateGameThreadAccess(*impl_, "RenderSystem::Submit must be called on the Game Thread.");
        SubmitFunction(std::move(command.debugName), std::move(command.function));
    }

    ErrorCode RenderSystem::Flush()
    {
        if (!impl_->commands.acceptingCommands.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        auto completed = std::make_shared<ManualResetEvent>(false);
        SubmitFunction("RenderSystemFlush", [completed](RenderThreadContext&) { completed->Set(); });

        completed->Wait();
        return ErrorCode::None;
    }

    ErrorCode RenderSystem::ExecuteSynchronous(std::string debugName, RenderSynchronousFunction function)
    {
        if (!function)
        {
            return ErrorCode::InvalidArgument;
        }

        if (!impl_->commands.acceptingCommands.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        auto completed = std::make_shared<ManualResetEvent>(false);
        auto operationResult = std::make_shared<ErrorCode>(ErrorCode::None);

        SubmitFunction(std::move(debugName),
                       [completed, operationResult, function = std::move(function)](RenderThreadContext& context)
                       {
                           *operationResult = function(context);
                           completed->Set();
                       });

        completed->Wait();
        return *operationResult;
    }

    void RenderSystem::SubmitFunction(std::string debugName, RenderCommandFunction function)
    {
        if (!function)
        {
            TerminateRenderSystem("submission failed: invalid command function", ErrorCode::InvalidArgument);
        }

        impl_->commands.activeSubmitCount.fetch_add(1, std::memory_order_acq_rel);
        auto submitCounterGuard =
            MakeScopeExit([this]() { impl_->commands.activeSubmitCount.fetch_sub(1, std::memory_order_acq_rel); });

        if (!impl_->commands.acceptingCommands.load(std::memory_order_acquire))
        {
            TerminateRenderSystem("submission failed: RenderSystem is not accepting commands", ErrorCode::InvalidState);
        }

        ErrorCode pushResult = impl_->commands.queue.Push(RenderCommand{std::move(debugName), std::move(function)});
        if (pushResult != ErrorCode::None)
        {
            TerminateRenderSystem("submission failed in RenderCommandQueue::Push", pushResult);
        }

        impl_->commands.semaphore.Release();
    }
} // namespace ve
