# VSM Fragment Page Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace physical-page-instanced VSM rasterization with one direct caster draw per mesh and one instance per active clipmap level, routing fragments into a shared `R32Uint` physical atlas through the page table.

**Architecture:** Extend the common RHI and FrameGraph with sampled storage textures, texture UAV barriers, and attachmentless fragment-UAV raster passes. Step6 clears dirty physical pages with a fixed compute dispatch; Step7 rasterizes each caster in virtual clipmap space and resolves the physical page per fragment. Forward shading loads the integer atlas directly and performs a manual depth comparison.

**Tech Stack:** C++20, HLSL Shader Model 5/6, CMake, FrameGraph, D3D11, D3D12, FXC, DXC, Win32 Editor smoke testing.

---

## Design Reference

Implement against:

- `Docs/Superpowers/Specs/2026-07-30-vsm-fragment-page-routing-design.md`
- Baseline commit `06c2911`
- Design commit `cb95a3a`

Do not reintroduce dirty-page lists, caster/page lists, fixed caster slices, indirect argument buffers, indirect draws, or physical-page-count caster instances.

## File Map

### Common RHI

- Modify `Engine/RHI/Common/RhiTypes.h`
  - Add `R32Uint`, texture `Storage`, `ReadWriteStorageTexture`, and the attachmentless-pass declaration.
- Modify `Engine/RHI/Common/RhiDevice.h`
  - Add read-write storage texture binding and texture UAV barrier APIs.
- Modify `Engine/RHI/D3D11/D3D11Rhi.cpp`
  - Create SRV/UAV-capable `R32Uint` textures and bind compute/fragment UAVs.
- Modify `Engine/RHI/D3D12/D3D12Rhi.cpp`
  - Create SRV/UAV descriptors, add UAV descriptor-table roots, transitions, and texture barriers.
- Modify `Engine/RHI/Metal/MetalRhi.mm`
  - Keep the interface buildable and fail explicitly for the unsupported path.

### FrameGraph

- Modify `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h`
  - Add texture `ShaderReadWrite`.
- Modify `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h`
- Modify `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.cpp`
  - Add texture writes and texture UAV barriers.
- Modify `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h`
- Modify `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp`
  - Version texture UAV writes, validate attachmentless fragment-UAV passes, and execute texture barriers.

### Virtual Shadow Resources And Sampling

- Modify `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h`
- Modify `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp`
- Modify `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- Modify `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Modify `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.h`
- Modify `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.cpp`
- Modify `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowFrameGraph.cpp`
  - Change the atlas to sampled/storage `R32Uint` and remove the comparison sampler.

### Virtual Shadow Passes

- Modify `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h`
  - Carry the page-table slice into page rendering.
- Modify `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep6_ClearPhysicalPages.cpp`
  - Replace raster clear with fixed compute clear.
- Modify `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep7_RenderCasters.cpp`
  - Replace physical-page instances with clipmap instances and fragment page routing.
- Modify `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep8_MarkRendered.cpp`
  - Preserve the atlas ordering edge after fragment writes.
- Modify `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
  - Remove the obsolete page-clear vertex count and document the integer encoding.

### Forward Sampling

- Modify `Assets/Builtin/Shaders/BasicMesh.hlsl`
  - Use `Texture2D<uint>.Load` and manual comparison.
- Modify `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
- Modify `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
  - Remove the VSM comparison-sampler resource declaration and validation.

No permanent unit-test files are added. Temporary RED probes described below must be removed before the final commit.

---

### Task 1: Establish The Baseline And RED Contracts

**Files:**

- Temporarily modify, then restore: `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp`
- Temporarily modify, then restore: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep7_RenderCasters.cpp`
- Inspect: `Docs/Superpowers/Specs/2026-07-30-vsm-fragment-page-routing-design.md`

- [ ] **Step 1: Confirm the branch starts at the approved design**

Run:

```powershell
git status --short
git log -3 --oneline
```

Expected:

```text
cb95a3a docs: design fragment-routed virtual shadows
06c2911 render: clarify virtual shadow frame graph scheduling
```

`git status --short` must be empty.

- [ ] **Step 2: Record the existing build and test baseline**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
```

Expected: configure, build, and every currently registered CTest pass.

- [ ] **Step 3: Record the performance baseline**

Launch:

```powershell
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

In SampleScene, enter Play and record:

```text
display refresh: 75 Hz
steady FPS: 75
requested pages:
cached pages:
redraw pages:
```

Expected: commit `06c2911` behavior remains a stable 75 FPS after warm-up.

- [ ] **Step 4: Make a temporary compile-failing RHI contract probe**

Temporarily change the atlas descriptor in `VirtualShadowSceneCache.cpp` to:

```cpp
rhi::RhiTextureDesc atlasDesc = {};
atlasDesc.width = desc_.atlasExtent;
atlasDesc.height = desc_.atlasExtent;
atlasDesc.format = rhi::RhiFormat::R32Uint;
atlasDesc.usage = static_cast<rhi::RhiTextureUsage>(
    static_cast<UInt32>(rhi::RhiTextureUsage::Sampled) |
    static_cast<UInt32>(rhi::RhiTextureUsage::Storage));
atlasDesc.debugName = "VirtualShadowSceneAtlas";
```

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: FAIL because `R32Uint` and `Storage` do not exist yet.

- [ ] **Step 5: Make a temporary compile-failing FrameGraph contract probe**

Temporarily change Step7 setup to:

```cpp
data.atlas = builder.Write(resources.atlas);
builder.AddUavBarrierBeforeExecute(data.atlas);
```

Run the same build command.

Expected: FAIL because texture `Write` and texture UAV barriers do not exist.

- [ ] **Step 6: Restore the temporary probes**

Restore only the two temporary edits manually and run:

```powershell
git diff -- Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp `
            Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep7_RenderCasters.cpp
```

Expected: no output. Do not commit this task.

---

### Task 2: Add The Common Sampled-Storage Texture Contract

**Files:**

- Modify: `Engine/RHI/Common/RhiTypes.h`
- Modify: `Engine/RHI/Common/RhiDevice.h`
- Modify: `Engine/RHI/Metal/MetalRhi.mm`
- Modify: `Engine/RHI/D3D11/D3D11Rhi.cpp`
- Modify: `Engine/RHI/D3D12/D3D12Rhi.cpp`

- [ ] **Step 1: Add the common enums and pass declaration**

In `RhiTypes.h`, add:

```cpp
enum class RhiPipelineResourceKind
{
    UniformBuffer,
    SampledTexture,
    Sampler,
    StorageBuffer,
    ReadWriteStorageBuffer,
    ReadWriteStorageTexture,
};
```

Add `R32Uint` before `Depth32Float`:

```cpp
/// One 32-bit unsigned integer channel.
R32Uint,
```

Extend texture usage:

```cpp
enum class RhiTextureUsage : uint32_t
{
    Sampled = 1 << 0,
    RenderTarget = 1 << 1,
    DepthStencil = 1 << 2,
    Storage = 1 << 3,
};
```

Extend `RhiRenderPassBeginInfo`:

```cpp
/// True only when an attachmentless graphics pass writes at least one fragment-stage UAV.
bool hasFragmentUavWrites = false;
```

- [ ] **Step 2: Add command-list texture APIs**

In `RhiDevice.h`, add:

```cpp
/// Binds a complete texture as a read-write storage texture.
virtual void SetReadWriteStorageTexture(RhiShaderStage stage, uint32_t slot, const RhiTexture& texture) = 0;

/// Makes prior unordered-access writes to the listed textures visible to following GPU commands.
virtual void InsertTextureUavBarriers(std::span<RhiTexture* const> textures) = 0;
```

Keep the existing buffer-only `InsertUavBarriers` API unchanged.

- [ ] **Step 3: Add buildable fail-fast backend stubs**

Before implementing native behavior, add overrides in all three backends. D3D11 and D3D12 may temporarily assert so the complete tree builds:

```cpp
void SetReadWriteStorageTexture(RhiShaderStage, uint32_t, const RhiTexture&) override
{
    VE_ASSERT_ALWAYS_MESSAGE(false, "Read-write storage textures are not implemented by this backend yet.");
}

void InsertTextureUavBarriers(std::span<RhiTexture* const>) override
{
}
```

In Metal, retain the explicit failure permanently:

```cpp
void SetReadWriteStorageTexture(RhiShaderStage, uint32_t, const RhiTexture&) override
{
    VE_ASSERT_ALWAYS_MESSAGE(false, "Metal read-write storage textures are unsupported in this milestone.");
}

void InsertTextureUavBarriers(std::span<RhiTexture* const>) override
{
    VE_ASSERT_ALWAYS_MESSAGE(false, "Metal texture UAV barriers are unsupported in this milestone.");
}
```

Metal must also reject `RhiTextureUsage::Storage` in texture creation with a clear backend error.

- [ ] **Step 4: Build the interface change**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: PASS. No production path calls the new methods yet.

- [ ] **Step 5: Commit the common contract**

```powershell
git add Engine/RHI/Common/RhiTypes.h Engine/RHI/Common/RhiDevice.h `
        Engine/RHI/Metal/MetalRhi.mm Engine/RHI/D3D11/D3D11Rhi.cpp Engine/RHI/D3D12/D3D12Rhi.cpp
git commit -m "rhi: define sampled storage texture contract"
```

---

### Task 3: Implement D3D11 Sampled-Storage Textures

**Files:**

- Modify: `Engine/RHI/D3D11/D3D11Rhi.cpp`

- [ ] **Step 1: Add `R32Uint`, storage validation, and bind flags**

Map the format:

```cpp
case RhiFormat::R32Uint:
    return DXGI_FORMAT_R32_UINT;
```

In `IsD3D11TextureDescSupported`, enforce:

```cpp
const bool storage = HasTextureUsage(desc.usage, RhiTextureUsage::Storage);
if (depthStencil)
{
    return desc.format == RhiFormat::Depth32Float && !renderTarget && !storage;
}
if (storage)
{
    return desc.format == RhiFormat::R32Uint && !renderTarget;
}
return desc.format != RhiFormat::Depth32Float || (!sampled && !renderTarget);
```

In `ToD3D11TextureBindFlags`, add:

```cpp
if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::Storage)) != 0)
{
    flags |= D3D11_BIND_UNORDERED_ACCESS;
}
```

- [ ] **Step 2: Make `D3D11Texture` own a UAV**

Add `ComPtr<ID3D11UnorderedAccessView>` to its constructor and member fields, and expose:

```cpp
[[nodiscard]] ID3D11UnorderedAccessView* GetUnorderedAccessView() const noexcept
{
    return unorderedAccessView_.Get();
}
```

In `CreateTexture`, create the full-resource UAV when `Storage` is present:

```cpp
ComPtr<ID3D11UnorderedAccessView> unorderedAccessView;
if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::Storage)) != 0)
{
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = ToDxgiFormat(desc.format);
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    result = device_->CreateUnorderedAccessView(texture.Get(), &uavDesc, &unorderedAccessView);
    if (FAILED(result))
    {
        SetLastError(MakeHResultError("ID3D11Device::CreateUnorderedAccessView texture", result));
        return nullptr;
    }
}
```

Pass the UAV into the `D3D11Texture` constructor.

- [ ] **Step 3: Track and unbind the Output Merger UAV**

Add:

```cpp
ID3D11UnorderedAccessView* activeFragmentTextureUav_ = nullptr;
```

Reset it in `Begin`. In `EndRenderPass`, unbind with:

```cpp
ID3D11UnorderedAccessView* nullUav = nullptr;
context_->OMSetRenderTargetsAndUnorderedAccessViews(
    D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,
    nullptr,
    nullptr,
    0,
    1,
    &nullUav,
    nullptr);
activeFragmentTextureUav_ = nullptr;
context_->OMSetRenderTargets(0, nullptr, nullptr);
```

Extend `ClearUnorderedAccessBindings` to clear all compute UAVs and, only when `activeFragmentTextureUav_` is non-null, clear fragment UAV `u0` with `D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL`. This makes `SetPipeline` safely move Step6 compute UAV ownership to Step7 Output Merger ownership without disturbing ordinary color/depth attachments. `EndRenderPass` clears the fragment UAV before forward `SetTexture` binds the atlas as an SRV.

- [ ] **Step 4: Implement compute and fragment texture UAV binding**

Replace the D3D11 stub with:

```cpp
void SetReadWriteStorageTexture(RhiShaderStage stage, uint32_t slot, const RhiTexture& texture) override
{
    if (!ValidateBinding(RhiPipelineResourceKind::ReadWriteStorageTexture, stage, slot))
    {
        return;
    }

    const auto& d3dTexture = static_cast<const D3D11Texture&>(texture);
    ID3D11UnorderedAccessView* uav = d3dTexture.GetUnorderedAccessView();
    VE_ASSERT_MESSAGE(uav != nullptr, "D3D11 read-write storage texture binding requires a UAV.");
    if (uav == nullptr)
    {
        return;
    }

    if (stage == RhiShaderStage::Compute)
    {
        context_->CSSetUnorderedAccessViews(slot, 1, &uav, nullptr);
        return;
    }

    VE_ASSERT_MESSAGE(stage == RhiShaderStage::Fragment, "D3D11 texture UAVs support only compute and fragment stages.");
    VE_ASSERT_MESSAGE(activeRenderTargetView_ == nullptr && activeDepthTexture_ == nullptr && slot == 0,
                      "D3D11 fragment texture UAV path requires an attachmentless pass at u0.");
    context_->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 1, &uav, nullptr);
    activeFragmentTextureUav_ = uav;
}
```

Keep `InsertTextureUavBarriers` a documented no-op because the immediate context orders UAV accesses.

- [ ] **Step 5: Permit only declared attachmentless fragment-UAV passes**

Change `BeginRenderPass` validation so no attachments are accepted only when:

```cpp
beginInfo.hasFragmentUavWrites
```

Reject `hasFragmentUavWrites` when either a color or depth attachment is present in this first implementation.

- [ ] **Step 6: Build D3D11**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: PASS with no D3D11 compile errors.

- [ ] **Step 7: Commit D3D11 support**

```powershell
git add Engine/RHI/D3D11/D3D11Rhi.cpp
git commit -m "rhi: support storage textures on d3d11"
```

---

### Task 4: Implement D3D12 Sampled-Storage Textures

**Files:**

- Modify: `Engine/RHI/D3D12/D3D12Rhi.cpp`

- [ ] **Step 1: Add format, usage validation, and resource flags**

Map:

```cpp
case RhiFormat::R32Uint:
    return DXGI_FORMAT_R32_UINT;
```

Validate storage exactly as D3D11 does, and add:

```cpp
if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::Storage)) != 0)
{
    flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
}
```

- [ ] **Step 2: Give `D3D12Texture` separate SRV and UAV descriptors**

Store:

```cpp
RhiNativeShaderResourceDescriptor shaderResourceDescriptor_ = {};
RhiNativeShaderResourceDescriptor unorderedAccessDescriptor_ = {};
```

Release both descriptors in the destructor. Add:

```cpp
[[nodiscard]] bool HasUnorderedAccessView() const noexcept;
[[nodiscard]] ID3D12DescriptorHeap* GetUnorderedAccessViewHeap() const noexcept;
[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetUnorderedAccessView() const noexcept;
```

Both descriptors come from `D3D12ShaderResourceDescriptorAllocator` so one shader-visible CBV/SRV/UAV heap remains active.

- [ ] **Step 3: Create the texture UAV descriptor**

In `CreateTexture`:

```cpp
RhiNativeShaderResourceDescriptor unorderedAccessDescriptor = {};
if ((usageValue & static_cast<uint32_t>(RhiTextureUsage::Storage)) != 0)
{
    if (!shaderResourceDescriptorAllocator_->Allocate(unorderedAccessDescriptor))
    {
        SetLastError("D3D12 UAV descriptor heap is exhausted.");
        return nullptr;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = ToDxgiFormat(desc.format);
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(
        resource.Get(), nullptr, &uavDesc, D3D12_CPU_DESCRIPTOR_HANDLE{unorderedAccessDescriptor.cpuHandle});
}
```

Pass it to `D3D12Texture`. Ensure a failed later allocation releases any descriptor already allocated.

- [ ] **Step 4: Represent storage textures as UAV descriptor tables**

In graphics and compute root-signature construction, classify:

```cpp
if (binding.kind == RhiPipelineResourceKind::ReadWriteStorageTexture)
{
    descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
}
else if (binding.kind == RhiPipelineResourceKind::SampledTexture)
{
    descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
}
else
{
    descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
}
```

Do not use a root UAV descriptor for textures.

- [ ] **Step 5: Implement binding, transitions, and texture UAV barriers**

Replace the D3D12 stub:

```cpp
void SetReadWriteStorageTexture(RhiShaderStage stage, uint32_t slot, const RhiTexture& texture) override
{
    UINT rootParameterIndex = 0;
    if (!ResolveRootParameter(RhiPipelineResourceKind::ReadWriteStorageTexture, stage, slot, rootParameterIndex))
    {
        return;
    }

    auto& d3dTexture = const_cast<D3D12Texture&>(static_cast<const D3D12Texture&>(texture));
    VE_ASSERT_MESSAGE(d3dTexture.HasUnorderedAccessView(), "D3D12 storage texture requires a UAV descriptor.");
    TransitionResource(d3dTexture.GetNativeResource(), d3dTexture.GetResourceState(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    d3dTexture.SetResourceState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    activeResourceHeap_ = d3dTexture.GetUnorderedAccessViewHeap();
    UpdateDescriptorTable(rootParameterIndex, d3dTexture.GetUnorderedAccessView());
    ApplyDescriptorHeapsAndTables();
}
```

Implement:

```cpp
void InsertTextureUavBarriers(std::span<RhiTexture* const> textures) override
{
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(textures.size());
    for (RhiTexture* texture : textures)
    {
        VE_ASSERT_MESSAGE(texture != nullptr, "D3D12 texture UAV barrier requires a valid texture.");
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = static_cast<D3D12Texture*>(texture)->GetNativeResource();
        barriers.push_back(barrier);
    }
    if (!barriers.empty())
    {
        commandList_->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    }
}
```

- [ ] **Step 6: Permit only declared attachmentless fragment-UAV passes**

Apply the same `hasFragmentUavWrites` validation as D3D11. Calling:

```cpp
commandList_->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
```

is valid for the attachmentless pass. Sampling later uses existing `SetTexture`, which transitions the atlas from `UNORDERED_ACCESS` to shader-resource state.

- [ ] **Step 7: Build and commit D3D12 support**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: PASS.

Commit:

```powershell
git add Engine/RHI/D3D12/D3D12Rhi.cpp
git commit -m "rhi: support storage textures on d3d12"
```

---

### Task 5: Add FrameGraph Texture UAV Writes And Attachmentless Raster

**Files:**

- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.cpp`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp`

- [ ] **Step 1: Add read-write texture access**

In `FrameGraphTextureAccess` add:

```cpp
ShaderReadWrite,
```

In `FrameGraphBuilder` add:

```cpp
/// Declares shader read-write access and returns the newly written logical texture version.
[[nodiscard]] FrameGraphTextureHandle Write(FrameGraphTextureHandle handle);

/// Orders prior UAV writes to one declared read-write texture before this pass executes.
void AddUavBarrierBeforeExecute(FrameGraphTextureHandle handle) noexcept;
```

Implement them with `WriteTexture(..., FrameGraphTextureAccess::ShaderReadWrite)` and the new FrameGraph overload.

- [ ] **Step 2: Store buffer and texture barriers separately**

Change `PassNode` and diagnostics to:

```cpp
std::vector<FrameGraphBufferHandle> bufferUavBarriersBeforeExecute;
std::vector<FrameGraphTextureHandle> textureUavBarriersBeforeExecute;
```

Update existing buffer call sites and diagnostics to use the renamed buffer field.

- [ ] **Step 3: Validate texture barriers and attachmentless raster**

For every texture barrier require a matching texture access:

```cpp
access.access == FrameGraphTextureAccess::ShaderReadWrite &&
access.output.index == barrier.index
```

For raster passes compute:

```cpp
const bool hasFragmentStorageWrite =
    std::any_of(pass.textureAccesses.begin(), pass.textureAccesses.end(),
                [](const TextureAccessRecord& access)
                {
                    return access.mode == TextureAccessMode::Write &&
                           access.access == FrameGraphTextureAccess::ShaderReadWrite;
                });
```

Validation rules:

```text
compute: no color/depth attachments
raster with attachment: existing rules remain
raster without attachment: requires hasFragmentStorageWrite
raster texture UAV barrier: allowed only for a declared ShaderReadWrite texture
raster buffer UAV barrier: remains invalid
```

Also require `RhiTextureUsage::Storage` for every `ShaderReadWrite` texture declaration.

- [ ] **Step 4: Populate the native begin packet**

In `BuildRenderPassBeginInfo` set:

```cpp
beginInfo.hasFragmentUavWrites =
    pass.raster &&
    std::any_of(pass.textureAccesses.begin(), pass.textureAccesses.end(),
                [](const TextureAccessRecord& access)
                {
                    return access.mode == TextureAccessMode::Write &&
                           access.access == FrameGraphTextureAccess::ShaderReadWrite;
                });
```

Keep it false for compute passes.

- [ ] **Step 5: Execute texture barriers**

Before `BeginRenderPass`, resolve each declared texture barrier and call:

```cpp
commandList.InsertTextureUavBarriers(barrierTextures);
```

Do this after existing buffer barriers and before opening the raster pass.

- [ ] **Step 6: Re-run the original FrameGraph RED probe**

Temporarily use:

```cpp
data.atlas = builder.Write(resources.atlas);
builder.AddUavBarrierBeforeExecute(data.atlas);
```

in Step7 and build.

Expected: the API now compiles. Runtime is not expected to work until the atlas and passes are migrated. Restore the temporary edit before committing.

- [ ] **Step 7: Build and commit**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: PASS.

Commit:

```powershell
git add Engine/Runtime/Render/Renderer/FrameGraph
git commit -m "render: support texture uav frame graph passes"
```

---

### Task 6: Migrate The Shared Atlas And Remove The Comparison Sampler

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowFrameGraph.cpp`

- [ ] **Step 1: Change atlas creation**

Replace the sampler and depth-atlas creation in `VirtualShadowSceneCache::EnsureResources` with:

```cpp
rhi::RhiTextureDesc atlasDesc = {};
atlasDesc.width = desc_.atlasExtent;
atlasDesc.height = desc_.atlasExtent;
atlasDesc.format = rhi::RhiFormat::R32Uint;
atlasDesc.usage = static_cast<rhi::RhiTextureUsage>(
    static_cast<UInt32>(rhi::RhiTextureUsage::Sampled) |
    static_cast<UInt32>(rhi::RhiTextureUsage::Storage));
atlasDesc.debugName = "VirtualShadowSceneAtlas";
std::unique_ptr<rhi::RhiTexture> atlasTexture = device.CreateTexture(atlasDesc);
if (atlasTexture == nullptr)
{
    FailVirtualShadow("VSM failed to create the R32Uint sampled-storage scene atlas.");
}
```

Remove `comparisonSampler_`, both getter overloads, creation, completeness checks, ownership transfer, and reset.

- [ ] **Step 2: Remove the sampler from prepared results**

`VirtualShadowViewResult` becomes:

```cpp
struct VirtualShadowViewResult
{
    VirtualShadowFramePacket packet;
    rhi::RhiTexture* atlas = nullptr;
    rhi::RhiBuffer* pageTable = nullptr;
    UInt64 pageTableOffset = 0;
    UInt64 pageTableSize = 0;
};
```

Remove the `RhiSampler` forward declaration and the manager assignment from `GetComparisonSampler`.

- [ ] **Step 3: Remove the sampler from sampling snapshots**

`VirtualShadowSamplingSnapshot` retains only:

```cpp
VirtualShadowGpuConstants constants;
rhi::RhiTexture* atlas = nullptr;
rhi::RhiBuffer* pageTable = nullptr;
UInt64 pageTableOffset = 0;
UInt64 pageTableSize = 0;
```

Rename `IsValidDepthTexture` to `IsValidVirtualShadowAtlas` and require `R32Uint`. Update family validation to require only atlas and page table. In `BindVirtualShadowSampling`, remove:

```cpp
commandList.SetSampler(rhi::RhiShaderStage::Fragment, 1, *snapshot.comparisonSampler);
```

- [ ] **Step 4: Change the imported FrameGraph descriptor**

Use:

```cpp
atlasDesc.usage = static_cast<rhi::RhiTextureUsage>(
    static_cast<UInt32>(rhi::RhiTextureUsage::Sampled) |
    static_cast<UInt32>(rhi::RhiTextureUsage::Storage));
```

Require the imported atlas format to be `R32Uint` before registration.

- [ ] **Step 5: Run the runtime creation probe**

Build and launch once with D3D11, then D3D12:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Expected at this intermediate point:

- Both backends create the sampled-storage atlas successfully.
- Rendering may terminate when the old Step6 attempts to use the integer atlas as a depth attachment.
- No comparison-sampler creation or binding remains.

- [ ] **Step 6: Commit the resource migration**

```powershell
git add Engine/Runtime/Render/VirtualShadow
git commit -m "render: migrate virtual shadow atlas to r32 uint"
```

---

### Task 7: Replace Step6 With A Fixed Compute Clear

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep6_ClearPhysicalPages.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPasses.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`

- [ ] **Step 1: Remove obsolete raster-clear constants**

Delete:

```cpp
constexpr UInt32 VirtualShadowPageClearVertexCount = 6;
```

Document that atlas value zero is empty and valid depths use positive reversed-float bit patterns.

- [ ] **Step 2: Replace Step6 HLSL**

Use this compute structure:

```hlsl
StructuredBuffer<PhysicalPage> PhysicalPages : register(t2);
RWTexture2D<uint> PhysicalAtlas : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    uint physicalIndex = groupID.x;
    if (physicalIndex >= physicalCapacity)
    {
        return;
    }

    PhysicalPage page = PhysicalPages[physicalIndex];
    bool matchesView = ((page.key1 >> 8u) & 0x00FFFFFFu) == (viewID & 0x00FFFFFFu);
    if ((page.flags & 7u) != 7u || !matchesView)
    {
        return;
    }

    uint pagesPerRow = atlasExtent / physicalPageSize;
    uint2 slotOrigin = uint2(physicalIndex % pagesPerRow, physicalIndex / pagesPerRow) * physicalPageSize;
    for (uint y = groupThreadID.y; y < physicalPageSize; y += 8u)
    {
        for (uint x = groupThreadID.x; x < physicalPageSize; x += 8u)
        {
            PhysicalAtlas[slotOrigin + uint2(x, y)] = 0u;
        }
    }
}
```

The mask `7u` means valid, dirty, and requested must all be set.

- [ ] **Step 3: Create and bind the compute pipeline**

Declare:

```cpp
const rhi::RhiPipelineResourceBindingDesc bindings[] = {
    {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
    {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Compute, 2},
    {rhi::RhiPipelineResourceKind::ReadWriteStorageTexture, rhi::RhiShaderStage::Compute, 0},
};
```

Bind and dispatch:

```cpp
commandList.SetComputePipeline(*pipeline);
commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
commandList.SetStorageBuffer(rhi::RhiShaderStage::Compute, 2, physicalPages, 0, physicalPages.GetSize());
commandList.SetReadWriteStorageTexture(rhi::RhiShaderStage::Compute, 0, atlas);
commandList.Dispatch(context.physicalPageCapacity, 1, 1);
```

- [ ] **Step 4: Register Step6 as a compute texture write**

Extend the pass declaration with a same-frame ordering flag:

```cpp
void AddVirtualShadowStep6_ClearPhysicalPagesPass(
    FrameGraph& frameGraph,
    const VirtualShadowPreparedViewGraphData& view,
    VirtualShadowFrameGraphResources& resources,
    bool atlasUavBarrierBeforeExecute = false);
```

Use:

```cpp
data.physicalPages = builder.Read(resources.physicalPages);
data.atlas = builder.Write(resources.atlas);
resources.atlas = data.atlas;
if (atlasUavBarrierBeforeExecute)
{
    builder.AddUavBarrierBeforeExecute(data.atlas);
}
```

Remove viewport, scissor, and depth attachment declarations. In execution resolve both `physicalPages` and `atlas`. The first rendered View relies on a D3D12 state transition from sampled to UAV; a later View requests a UAV barrier because the preceding View left the shared atlas in UAV state.

- [ ] **Step 5: Compile both shader models through runtime compilation**

Build and launch D3D11 and D3D12 Editor once.

Expected: `VirtualShadow.Step6_ClearPhysicalPages.Compute` compiles under `cs_5_0` and `cs_6_0`, and no depth-attachment error occurs in Step6.

- [ ] **Step 6: Commit Step6**

```powershell
git add Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep6_ClearPhysicalPages.cpp `
        Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPasses.h `
        Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h
git commit -m "render: clear dirty virtual shadow pages in compute"
```

---

### Task 8: Route Step7 Fragments Through The Page Table

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep7_RenderCasters.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep8_MarkRendered.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`

- [ ] **Step 1: Carry the page-table slice into page rendering**

Add to `VirtualShadowPageRecordingContext`:

```cpp
VirtualShadowPageTableSlice slice;
```

Add to `VirtualShadowViewPagePassData`:

```cpp
VirtualShadowPageTableSlice slice;
FrameGraphBufferHandle pageTable;
```

Copy `view.slice` during Step7 setup and propagate it in `BuildVirtualShadowPagePassContext`. Validate:

```cpp
slice.pageTableSize == VirtualShadowLogicalPageBufferSize
slice.pageTableOffset % sizeof(UInt32) == 0
```

- [ ] **Step 2: Replace Step7 vertex output**

The vertex shader must rasterize the complete virtual clipmap:

```hlsl
cbuffer ObjectConstants : register(b2)
{
    float4x4 localToWorld;
    uint receiveShadows;
    uint3 objectPadding;
};

struct Input
{
    float3 position : POSITION;
};

struct Output
{
    float4 position : SV_POSITION;
    nointerpolation uint level : TEXCOORD0;
    float normalizedDepth : TEXCOORD1;
};

Output VSMain(Input input, uint instanceID : SV_InstanceID)
{
    Output output;
    uint level = instanceID;
    float4 world = mul(localToWorld, float4(input.position, 1.0f));
    float3 light = float3(
        dot(world.xyz, lightRight.xyz),
        dot(world.xyz, lightUp.xyz),
        dot(world.xyz, lightForward.xyz));

    float pageWorldSize = clipmaps[level].originAndPageSize.w;
    float2 workingRegionMinimum =
        float2(clipmaps[level].pageData.xy - int2(64, 64)) * pageWorldSize;
    float2 virtualUv = (light.xy - workingRegionMinimum) /
                       (pageWorldSize * 128.0f);

    float depthRange = clipmaps[level].radiusAndDepth.z -
                       clipmaps[level].radiusAndDepth.y;
    output.position = float4(
        virtualUv.x * 2.0f - 1.0f,
        1.0f - virtualUv.y * 2.0f,
        0.0f,
        1.0f);
    output.level = level;
    output.normalizedDepth =
        (light.z - clipmaps[level].radiusAndDepth.y) / depthRange;
    return output;
}
```

- [ ] **Step 3: Add fragment page routing**

Bind:

```hlsl
StructuredBuffer<uint> PageTable : register(t5);
StructuredBuffer<PhysicalPage> PhysicalPages : register(t2);
RWTexture2D<uint> PhysicalAtlas : register(u0);
```

Use:

```hlsl
void PSMain(Output input)
{
    if (input.level >= clipmapCount ||
        input.normalizedDepth < 0.0f ||
        input.normalizedDepth > 1.0f)
    {
        discard;
    }

    uint2 virtualPixel = uint2(input.position.xy);
    if (any(virtualPixel >= uint2(16384u, 16384u)))
    {
        discard;
    }

    uint2 localPage = virtualPixel / physicalPageSize;
    uint2 pagePixel = virtualPixel % physicalPageSize;
    uint logicalIndex =
        input.level * 16384u + localPage.y * 128u + localPage.x;
    uint denseEntry = PageTable[logicalIndex];
    if (denseEntry == 0u)
    {
        discard;
    }

    uint physicalIndex = denseEntry - 1u;
    if (physicalIndex >= physicalCapacity)
    {
        discard;
    }

    PhysicalPage page = PhysicalPages[physicalIndex];
    bool matchesView =
        ((page.key1 >> 8u) & 0x00FFFFFFu) ==
        (viewID & 0x00FFFFFFu);
    bool matchesLevel = (page.key1 & 0xFFu) == input.level;
    if ((page.flags & 7u) != 7u || !matchesView || !matchesLevel)
    {
        discard;
    }

    uint pagesPerRow = atlasExtent / physicalPageSize;
    uint2 slotOrigin =
        uint2(physicalIndex % pagesPerRow, physicalIndex / pagesPerRow) *
        physicalPageSize;
    float reversedDepth = max(
        1.0f - saturate(input.normalizedDepth),
        asfloat(1u));
    InterlockedMax(
        PhysicalAtlas[slotOrigin + pagePixel],
        asuint(reversedDepth));
}
```

- [ ] **Step 4: Build an attachmentless graphics pipeline**

Compile both `VSMain` and `PSMain`. Resource layout:

```cpp
const rhi::RhiPipelineResourceBindingDesc bindings[] = {
    {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 2},
    {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 4},
    {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 4},
    {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Fragment, 2},
    {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Fragment, 5},
    {rhi::RhiPipelineResourceKind::ReadWriteStorageTexture, rhi::RhiShaderStage::Fragment, 0},
};
```

Pipeline state:

```cpp
desc.depthStencilState = rhi::StaticRenderStates::DepthDisabled;
desc.boundShaderState.vertexShader = vertexShader;
desc.boundShaderState.fragmentShader = fragmentShader;
desc.colorAttachmentCount = 0;
desc.colorFormat = rhi::RhiFormat::Unknown;
desc.depthFormat = rhi::RhiFormat::Unknown;
```

- [ ] **Step 5: Bind per-view resources and draw clipmap instances**

Bind:

```cpp
commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 2, *objectUniform.buffer, objectUniform.offset, objectUniform.size);
commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 4, *constants.buffer, constants.offset, constants.size);
commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 4, *constants.buffer, constants.offset, constants.size);
commandList.SetStorageBuffer(rhi::RhiShaderStage::Fragment, 2, physicalPages, 0, physicalPages.GetSize());
commandList.SetStorageBuffer(
    rhi::RhiShaderStage::Fragment, 5, pageTable, context.slice.pageTableOffset, VirtualShadowLogicalPageBufferSize);
commandList.SetReadWriteStorageTexture(rhi::RhiShaderStage::Fragment, 0, atlas);
```

Replace both physical-capacity instance counts with:

```cpp
VirtualShadowClipmapLevelCount
```

There remains exactly one direct draw per caster.

- [ ] **Step 6: Register Step7 dependencies and virtual viewport**

Setup:

```cpp
data.physicalPages = builder.Read(resources.physicalPages);
data.pageTable = builder.Read(resources.pageTable);
data.atlas = builder.Write(resources.atlas);
resources.atlas = data.atlas;
builder.AddUavBarrierBeforeExecute(data.atlas);
builder.SetRenderArea({0, 0, VirtualShadowVirtualResolution, VirtualShadowVirtualResolution});
builder.SetViewport({0.0f, 0.0f,
                     static_cast<Float32>(VirtualShadowVirtualResolution),
                     static_cast<Float32>(VirtualShadowVirtualResolution),
                     0.0f, 1.0f});
builder.SetScissor({0, 0, VirtualShadowVirtualResolution, VirtualShadowVirtualResolution});
```

The texture UAV barrier orders Step6 clear before Step7 fragment atomics.

- [ ] **Step 7: Make allocation through mark-rendered contiguous per View**

In `VirtualShadowManager::AddToFrameGraph`, keep Step1, all Step2 passes, and all Step3 passes as family-wide preparation. Replace the separate Step4 loop, Step5 loop, and page-render loop with one View loop:

```cpp
bool atlasWrittenThisFrame = false;
for (SizeT viewIndex = 0; viewIndex < family.views.size(); ++viewIndex)
{
    const VirtualShadowPreparedViewGraphData& view = family.views[viewIndex];
    AddVirtualShadowStep4_ResolvePageHitsPass(
        frameGraph,
        view,
        resources,
        viewIndex == 0,
        !view.packet.requiresRequestUpdate);
    AddVirtualShadowStep5_AllocatePagesPass(
        frameGraph,
        view,
        resources,
        viewIndex == 0);

    if (!view.packet.requiresPageRendering)
    {
        continue;
    }

    AddVirtualShadowStep6_ClearPhysicalPagesPass(
        frameGraph,
        view,
        resources,
        atlasWrittenThisFrame);
    AddVirtualShadowStep7_RenderCastersPass(frameGraph, view, resources);
    AddVirtualShadowStep8_MarkRenderedPass(frameGraph, view, resources);
    atlasWrittenThisFrame = true;
}
```

After the loop, copy the latest `resources.physicalPages` and `resources.statistics` into `family`, then register Step9 once.

- [ ] **Step 8: Preserve Step7-to-Step8 ordering**

Keep Step8's:

```cpp
data.atlas = builder.Read(resources.atlas);
```

This read creates the graph dependency after the Step7 atlas version. Step8 continues marking requested+dirty pages rendered and updating redraw statistics.

- [ ] **Step 9: Strict-compile the final inline shaders with FXC and DXC**

Using `apply_patch`, create disposable files:

```text
Build/vsm-inline-shader-check/Step6.hlsl
Build/vsm-inline-shader-check/Step7.hlsl
```

Each file must contain the final `VirtualShadowCommonHlsl` text followed by the exact Step6 or Step7 raw HLSL string from the corresponding `.cpp` file. Then run:

```powershell
$dxcPath = ((Select-String -Path Build/windows-msvc-debug/CMakeCache.txt -Pattern '^VE_DXC_EXECUTABLE:FILEPATH=').Line -split '=', 2)[1]
$fxcPath = ((Select-String -Path Build/windows-msvc-debug/CMakeCache.txt -Pattern '^VE_FXC_EXECUTABLE:FILEPATH=').Line -split '=', 2)[1]
& $fxcPath /T cs_5_0 /E CSMain /Ges /WX /Fo Build/vsm-inline-shader-check/Step6.dxbc Build/vsm-inline-shader-check/Step6.hlsl
& $dxcPath -T cs_6_0 -E CSMain -Ges -WX -Fo Build/vsm-inline-shader-check/Step6.dxil Build/vsm-inline-shader-check/Step6.hlsl
& $fxcPath /T vs_5_0 /E VSMain /Ges /WX /Fo Build/vsm-inline-shader-check/Step7VS.dxbc Build/vsm-inline-shader-check/Step7.hlsl
& $fxcPath /T ps_5_0 /E PSMain /Ges /WX /Fo Build/vsm-inline-shader-check/Step7PS.dxbc Build/vsm-inline-shader-check/Step7.hlsl
& $dxcPath -T vs_6_0 -E VSMain -Ges -WX -Fo Build/vsm-inline-shader-check/Step7VS.dxil Build/vsm-inline-shader-check/Step7.hlsl
& $dxcPath -T ps_6_0 -E PSMain -Ges -WX -Fo Build/vsm-inline-shader-check/Step7PS.dxil Build/vsm-inline-shader-check/Step7.hlsl
```

Expected: all six commands return exit code 0 with no warnings. Remove the disposable `.hlsl`, `.dxbc`, and `.dxil` files with `apply_patch` after recording the result.

- [ ] **Step 10: Build, run both backends, and inspect draw counts**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Expected:

- Step7 shader compiles under D3D11 and D3D12.
- No attachment validation error.
- No D3D debug-layer UAV/SRV hazard.
- `indexedDrawCount` equals indexed shadow caster count, not caster count multiplied by physical pages.
- Every indexed draw uses four instances with the current `[7, 10]` configuration.

- [ ] **Step 11: Commit fragment routing**

```powershell
git add Engine/Runtime/Render/VirtualShadow/FrameGraph `
        Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp
git commit -m "render: route virtual shadow fragments into physical pages"
```

---

### Task 9: Replace Forward Comparison Sampling With Integer Loads

**Files:**

- Modify: `Assets/Builtin/Shaders/BasicMesh.hlsl`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`

- [ ] **Step 1: Change the atlas declaration**

Replace:

```hlsl
Texture2D<float> VirtualShadowAtlas : register(t1, space0);
SamplerComparisonState VirtualShadowSampler : register(s1, space0);
```

with:

```hlsl
Texture2D<uint> VirtualShadowAtlas : register(t1, space0);
```

- [ ] **Step 2: Replace `SampleCmpLevelZero`**

Implement:

```hlsl
float SampleVirtualShadowPage(
    uint physicalPageIndex,
    float2 pagePosition,
    float receiverDepth)
{
    uint pagesPerRow =
        virtualShadowAtlasExtent / virtualShadowPhysicalPageSize;
    uint2 physicalPage = uint2(
        physicalPageIndex % pagesPerRow,
        physicalPageIndex / pagesPerRow);
    uint2 pageOrigin =
        physicalPage * virtualShadowPhysicalPageSize;
    uint2 pagePixel = min(
        uint2(saturate(float2(pagePosition.x, 1.0f - pagePosition.y)) *
              virtualShadowPhysicalPageSize),
        uint2(virtualShadowPhysicalPageSize - 1u,
              virtualShadowPhysicalPageSize - 1u));
    uint encodedDepth =
        VirtualShadowAtlas.Load(int3(pageOrigin + pagePixel, 0));
    if (encodedDepth == 0u)
    {
        return 1.0f;
    }

    float casterDepth = 1.0f - asfloat(encodedDepth);
    return receiverDepth <= casterDepth ? 1.0f : 0.0f;
}
```

Keep the existing normal bias before page lookup. Keep the existing normalized depth bias subtraction in `TryResolveVirtualShadowPage`; do not subtract it a second time in `SampleVirtualShadowPage`.

- [ ] **Step 3: Remove sampler requirements from forward passes**

In both Opaque and Transparent validation, require:

```cpp
atlas.texture != nullptr
table.buffer != nullptr
sampling.atlas != nullptr
sampling.pageTable != nullptr
```

Remove:

```cpp
{rhi::RhiPipelineResourceKind::Sampler, rhi::RhiShaderStage::Fragment, 1},
```

Keep:

```cpp
{rhi::RhiPipelineResourceKind::SampledTexture, rhi::RhiShaderStage::Fragment, 1},
```

- [ ] **Step 4: Compile the offline BasicMesh artifacts**

Create a disposable output directory under `Build` and run:

```powershell
$dxcPath = ((Select-String -Path Build/windows-msvc-debug/CMakeCache.txt -Pattern '^VE_DXC_EXECUTABLE:FILEPATH=').Line -split '=', 2)[1]
$fxcPath = ((Select-String -Path Build/windows-msvc-debug/CMakeCache.txt -Pattern '^VE_FXC_EXECUTABLE:FILEPATH=').Line -split '=', 2)[1]
$slangPath = ((Select-String -Path Build/windows-msvc-debug/CMakeCache.txt -Pattern '^VE_SLANG_EXECUTABLE:FILEPATH=').Line -split '=', 2)[1]
Build/windows-msvc-debug/Debug/VEngineShaderTool.exe compile `
  --source Assets/Builtin/Shaders/BasicMesh.hlsl `
  --output Build/vsm-basicmesh-shader-check `
  --name BasicMesh `
  --dxc $dxcPath `
  --fxc $fxcPath `
  --slang $slangPath
```

Expected:

- FXC vertex/pixel compilation succeeds.
- DXC vertex/pixel compilation succeeds.
- Reflection contains `VirtualShadowAtlas` at `t1`.
- Reflection does not contain `VirtualShadowSampler` at `s1`.

The generated check directory is not staged.

- [ ] **Step 5: Build, smoke, and commit forward sampling**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Launch the Editor with D3D11 and D3D12 and verify opaque and transparent materials render without resource-layout errors.

Commit:

```powershell
git add Assets/Builtin/Shaders/BasicMesh.hlsl `
        Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp `
        Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp
git commit -m "render: sample virtual shadows from integer atlas"
```

---

### Task 10: Verify Multi-View Ordering, Cache Semantics, And Performance

**Files:**

- Inspect: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Inspect: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep5_AllocatePages.cpp`
- Inspect: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep6_ClearPhysicalPages.cpp`
- Inspect: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep7_RenderCasters.cpp`
- Inspect: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep8_MarkRendered.cpp`
- Inspect: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep9_FinalizeScene.cpp`

- [ ] **Step 1: Audit the per-view chain**

Confirm `VirtualShadowManager::AddToFrameGraph` registers:

```text
Step4 Resolve View A
Step5 Allocate View A
Step6 Clear View A
Step7 Raster View A
Step8 MarkRendered View A
Step4 Resolve View B
Step5 Allocate View B
Step6 Clear View B
Step7 Raster View B
Step8 MarkRendered View B
```

Dependencies must serialize the shared resources across the contiguous View chains:

```text
physicalPages versions: Step5 A -> Step6/7 A reads -> Step8 A -> Step4/5 B -> Step6/7 B reads -> Step8 B
atlas versions: Step6 A -> Step7 A -> Step8 A read -> Step6 B -> Step7 B -> Step8 B read
pageTable versions: Step5 A -> Step7 A slice read -> Step4/5 B -> Step7 B slice read
```

If diagnostics show Step6 B before Step8 A, fix resource version propagation in `resources.atlas` or `resources.physicalPages`; do not add an out-of-graph manual fence.

- [ ] **Step 2: Audit shader isolation**

Confirm Step6 checks:

```text
valid + dirty + requested + ViewID
```

Confirm Step7 checks:

```text
mapped page + capacity + valid + dirty + requested + ViewID + clipmap level
```

Confirm neither shader writes a clean cached page.

- [ ] **Step 3: Run the complete build and existing tests**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
```

Expected: all commands pass.

- [ ] **Step 4: Run D3D11 Editor smoke**

Launch the Editor configured for D3D11:

```powershell
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Expected:

- Editor enters SampleScene.
- Entering Play does not crash.
- VSM shadows are visible.
- No D3D11 debug-layer SRV/UAV conflict.
- Stable frames show cached pages and zero redraw when nothing changes.
- Moving a caster causes redraw, then returns to cache reuse.

- [ ] **Step 5: Run D3D12 Editor smoke**

Switch the project/backend setting to D3D12 and launch with the same `--project` command.

Expected:

- Same visual and cache behavior as D3D11.
- No D3D12 validation error for UAV descriptor tables, attachmentless raster, resource state transitions, or missing UAV barriers.

- [ ] **Step 6: Validate two Views**

Render GameView and SceneView in the same family.

Expected:

- Both sample the same atlas object.
- Each uses its own page-table offset and ViewID.
- Moving one View does not corrupt cached pages belonging to the other View.
- Later-View rendering does not alter earlier-View clean cached pages.

- [ ] **Step 7: Validate performance acceptance**

On the 75 Hz display:

```text
SampleScene Play steady FPS: 75
caster draw calls: one per caster
instances per caster: 4
instances per caster proportional to physicalPageCapacity: no
dirty-page list pass: absent
caster/page culling pass: absent
indirect draws: absent
```

Compare against the Task 1 baseline. If FPS is below `06c2911`, capture CPU and GPU timings before changing architecture; do not add fixed caster slices or indirect-page draws as a fallback.

- [ ] **Step 8: Remove disposable probes and scan forbidden mechanisms**

Run:

```powershell
rg -n "DirtyPhysicalPageList|CasterPageList|DrawInstancedIndirect|DrawIndexedInstancedIndirect|physicalPageCapacity.*Draw|comparisonSampler|VirtualShadowSampler" Engine Assets
git status --short
```

Expected:

- No forbidden VSM mechanism or comparison sampler remains.
- Only intended source changes are present.
- `Build/vsm-basicmesh-shader-check` is untracked/ignored and not staged.

- [ ] **Step 9: Final cleanup commit if verification required small fixes**

If verification produced source fixes:

```powershell
git add Engine Assets
git commit -m "render: finalize fragment routed virtual shadows"
```

If no source fixes were needed, do not create an empty commit.

- [ ] **Step 10: Record final evidence**

Run:

```powershell
git status --short
git log --oneline 06c2911..HEAD
```

Expected: clean worktree and a short commit chain covering common RHI, D3D11, D3D12, FrameGraph, atlas migration, compute clear, fragment routing, and forward sampling.
