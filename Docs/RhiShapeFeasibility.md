# D3D11, D3D12, And Metal RHI Shape Feasibility

## 1. Purpose

This document analyzes whether VEngine can use one common RHI shape across D3D11, D3D12, and Metal.

The goal is to define a practical first-stage RHI design that:

- Maps naturally to D3D12 and Metal.
- Keeps D3D11 viable as a compatibility backend.
- Avoids forcing modern explicit APIs into a D3D11-style immediate-context design.
- Produces a small set of smoke tests before the full renderer is implemented.

## 2. Reference Models

Unreal Engine RHI is the closest conceptual reference: it is a render hardware interface over platform-specific graphics APIs, and Epic's public tutorial describes it as designed from the ground up for DirectX 12, Vulkan, and Metal 2.0.

Diligent Engine is a useful open-source reference because it exposes a common front-end API over modern and legacy backends. Its documentation describes it as a lightweight cross-platform graphics API abstraction library designed to take advantage of Direct3D12, Vulkan, and Metal while supporting older APIs such as Direct3D11. It also uses HLSL as a universal shading language across platforms.

bgfx is a useful boundary reference: it is cross-platform, graphics API agnostic, and supports D3D11, D3D12, and Metal, but its abstraction is higher-level than VEngine's target RHI. VEngine should borrow the "bring your own engine/framework" separation mindset, not the full API shape.

The Forge is another practical reference. It demonstrates that a low-level cross-platform rendering framework can target Windows with DirectX 12 and Apple platforms with Metal while remaining usable as a rendering layer inside custom engines.

## 3. Feasibility Verdict

The common RHI is feasible if it is shaped around modern explicit graphics API concepts.

Recommended direction:

```text
Use a D3D12/Metal-style explicit RHI as the common model.
Map D3D12 and Metal directly.
Map D3D11 through a compatibility layer.
```

This avoids the biggest architectural trap: if the common RHI is modeled after D3D11 immediate-context state changes, D3D12 and Metal would need to recover command order, resource state, synchronization, and lifetime information after the public API has already erased it.

The first-stage RHI should not expose every advanced D3D12 or Metal feature. It should prove the API shape through a small forward-rendering slice: clear, triangle, indexed mesh, constant buffer, texture, sampler, depth buffer, and present.

## 4. Common RHI Object Model

First-stage common objects:

```text
RhiInstance
RhiAdapter
RhiDevice
RhiQueue
RhiCommandAllocator
RhiCommandList
RhiSwapchain
RhiBuffer
RhiTexture
RhiTextureView
RhiSampler
RhiShaderModule
RhiPipelineState
RhiBindGroupLayout
RhiBindGroup
RhiRenderPass
RhiFramebuffer
```

Objects should be explicit and owned. Avoid hidden global graphics state in the common API.

## 5. Backend Mapping

### 5.1 Device

```text
RhiDevice
  D3D11: ID3D11Device + immediate context owner
  D3D12: ID3D12Device
  Metal: MTLDevice
```

D3D11 exposes both device and context; VEngine should hide this split behind the D3D11 backend. D3D12 and Metal map more directly.

### 5.2 Queue

```text
RhiQueue
  D3D11: immediate-context submission adapter
  D3D12: ID3D12CommandQueue
  Metal: MTLCommandQueue
```

First stage should expose only one graphics queue. Copy and compute queues can be reserved for later.

### 5.3 Command List

```text
RhiCommandList
  D3D11: compatibility command recorder or immediate-context executor
  D3D12: ID3D12GraphicsCommandList
  Metal: MTLCommandBuffer + active command encoder
```

The common API should be command-list-shaped even when D3D11 executes commands immediately. This preserves the Render Thread contract and keeps D3D12/Metal natural.

D3D11 deferred contexts should not be required in the first implementation. They can be evaluated later if profiling shows real benefit.

### 5.4 Swapchain

```text
RhiSwapchain
  D3D11: IDXGISwapChain
  D3D12: IDXGISwapChain
  Metal: CAMetalLayer drawable provider
```

Common RHI public headers should not expose `HWND`, `IDXGISwapChain`, `CAMetalLayer`, Objective-C types, or Metal headers directly.

Platform-specific surface descriptors may live in backend-private headers.

## 6. Command Model

First-stage `RhiCommandList` commands:

```text
Begin()
End()
BeginRenderPass()
EndRenderPass()
SetPipeline()
SetBindGroup()
SetVertexBuffer()
SetUniformBuffer()
SetIndexBuffer()
SetViewport()
SetScissor()
Draw()
DrawIndexed()
```

Backend behavior:

- D3D12 records native command lists.
- Metal creates command buffers and encoders.
- D3D11 may record lightweight VEngine commands and replay them into the immediate context on the Render Thread, or execute directly while preserving the same public API.

The common command model should not allow arbitrary state mutation outside command recording.

## 7. Resource State Model

Common resources should track a simplified `RhiResourceState`.

First-stage states:

```text
Undefined
CopySource
CopyDestination
VertexBuffer
IndexBuffer
ConstantBuffer
ShaderRead
RenderTarget
DepthWrite
DepthRead
Present
```

Mapping:

- D3D12 maps states to resource barriers.
- Metal maps states to encoder boundaries, usage declarations, load/store actions, and explicit synchronization only where needed.
- D3D11 treats state transitions as validation and hazard bookkeeping; most transitions are no-op at the native API level.

This state model can be refined later into separate layout/access/synchronization concepts if Metal or future Vulkan support needs more precision.

## 8. Binding Model

Use bind groups rather than D3D11-style ad hoc slot-setting as the common model.

First-stage objects:

```text
RhiBindGroupLayout
RhiBindGroup
RhiBinding
```

First-stage supported bindings:

```text
Constant buffer
Texture SRV
Sampler
```

Mapping:

```text
D3D11
  Constant buffers, shader resource views, and sampler states per shader stage.

D3D12
  Descriptor tables and root signatures.

Metal
  Direct buffer/texture/sampler slots first.
  Argument buffers can be added later.
```

Binding layouts should be derived from shader reflection where possible. The first stage can keep explicit binding metadata in shader asset descriptions until the shader tool is mature.

## 9. Pipeline Model

`RhiPipelineState` should be immutable after creation.

First-stage pipeline description:

```text
Shader modules
Vertex layout
Primitive topology
Rasterizer state
Depth/stencil state
Blend state
Render target formats
Depth/stencil format
Sample count
Bind group layouts
```

Mapping:

```text
D3D11
  Input layout + shaders + rasterizer/depth/blend state objects.

D3D12
  ID3D12PipelineState + root signature.

Metal
  MTLRenderPipelineState + MTLDepthStencilState.
```

The common API should avoid mutable pipeline state bags. If a state combination affects native pipeline creation on D3D12 or Metal, it belongs in the pipeline descriptor.

## 10. Render Pass Model

Use an explicit render pass descriptor even though D3D11 does not have a native render pass object.

First-stage render pass fields:

```text
Color attachments
Depth/stencil attachment
Load actions
Store actions
Clear values
Attachment formats
Sample count
```

Mapping:

```text
D3D11
  OMSetRenderTargets + ClearRenderTargetView + ClearDepthStencilView.

D3D12
  RTV/DSV binding + barriers + clears.

Metal
  MTLRenderPassDescriptor.
```

This model keeps Metal natural and gives D3D12 enough information for barriers and render target setup.

## 11. Surface And Swapchain Model

The common RHI should own an abstract surface descriptor.

Recommended split:

```text
RhiSurfaceDesc
  width
  height
  format
  presentMode
  platformHandle
```

Backend-private descriptors may reinterpret `platformHandle`.

Examples:

```text
Windows D3D11/D3D12
  platformHandle -> HWND, kept out of common public headers.

iOS Metal
  platformHandle -> CAMetalLayer, kept out of common public headers.
```

The platform layer creates the native window/layer. The RHI consumes only the backend-appropriate surface data.

## 12. Minimum Backend Feature Contract

Every first-stage backend should implement:

```text
Create device
Create graphics queue
Create swapchain/surface
Create buffers
Create textures
Create shader modules
Create graphics pipeline
Create bind groups
Record command list
Begin/end render pass
Draw indexed static mesh
Present
```

Anything outside this contract should be treated as a later extension.

## 13. Prototype Stages

Recommended prototype order:

```text
Stage 1: D3D11 + D3D12 device and swapchain creation on Windows.
Stage 2: D3D11 + D3D12 clear-color render pass.
Stage 3: D3D11 + D3D12 triangle with one pipeline and one vertex buffer.
Stage 4: D3D11 + D3D12 indexed mesh with constant buffer, texture, and sampler.
Stage 5: Per-frame render contexts.
Stage 6: Metal device, CAMetalLayer surface, clear-color render pass on iOS Simulator.
Stage 7: Metal triangle using the same high-level RHI client code shape.
Stage 8: Metal indexed mesh with constant buffer, texture, and sampler.
```

Windows should come first because it allows D3D11 and D3D12 to be compared side by side with the same OS windowing path. Metal should be added before the RHI grows beyond the first mesh-rendering slice.

## 14. Acceptance Criteria

The RHI shape is accepted when:

- The same Render-layer client code can issue the triangle and indexed-mesh smoke test against D3D11, D3D12, and Metal.
- Common RHI headers do not include D3D, DXGI, Objective-C, UIKit, or Metal headers.
- D3D11-specific implementation details do not change the common RHI API shape.
- Debug builds validate invalid resource states, missing bindings, incompatible pipeline/render-pass formats, and use-after-free handles.
- The first implementation supports a small render frame context ring.

## 15. Main Risks

### 15.1 D3D11 Overfitting

If VEngine starts by exposing D3D11-like immediate state changes, D3D12 and Metal will become awkward and error-prone.

Mitigation:

- Keep command list, bind group, pipeline, render pass, and resource state concepts in the common API from the beginning.

### 15.2 Overbuilding D3D12 Concepts

If VEngine exposes all D3D12 details immediately, the first RHI will become too heavy.

Mitigation:

- Start with one graphics queue, simple descriptor/bind group allocation, and one forward-rendering path.

### 15.3 Metal Binding Mismatch

Metal can start with direct slot binding, while D3D12 benefits from descriptor tables.

Mitigation:

- Keep bind groups as the common concept, but let the first Metal backend map them to direct slots. Add argument buffers later only if needed.

### 15.4 Lifetime Hazards

D3D12 and Metal require careful resource lifetime management across frames.

Mitigation:

- Introduce frames-in-flight and fence/completion tracking before adding dynamic resources or streaming uploads.

## 16. Open Questions

- Should D3D11 deferred contexts be used in the first stage, or should the D3D11 backend begin with immediate-context execution on the Render Thread?
- Should Metal argument buffers be planned from the start or introduced after the direct-slot implementation works?
- Should `RhiResourceState` remain a single enum in the first stage or split into access/layout/synchronization concepts earlier?
- Should `RhiBindGroup` support dynamic constant-buffer offsets in the first stage, or defer them until UI/material batching needs them?

## 17. Reference URLs

```text
Unreal RHI:
https://dev.epicgames.com/community/learning/tutorials/aqV9/unreal-engine-render-hardware-interface-rhi

Diligent Engine:
https://diligentgraphics.com/diligent-engine/
https://diligentgraphics.com/diligent-engine/using-the-api/

bgfx:
https://bkaradzic.github.io/bgfx/
https://bkaradzic.github.io/bgfx/overview.html

The Forge:
https://github.com/ConfettiFX/The-Forge
https://theforge.dev/

D3D11 multithreaded rendering:
https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-render

D3D12 programming guide:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/directx-12-programming-guide

Metal command structure:
https://developer.apple.com/documentation/metal/setting-up-a-command-structure
```
