# VSM Fragment Page Routing Design

## Goal

Replace physical-page-instanced virtual-shadow rasterization with a fragment-routed physical atlas. Each shadow caster is rasterized once with one instance per active clipmap level. The pixel shader resolves the logical page through the page table and atomically writes the corresponding physical atlas pixel.

The implementation must support D3D11 and D3D12, preserve the render-system-wide physical page pool and per-view page tables, and maintain the `06c2911` SampleScene Play performance baseline of a stable 75 FPS on a 75 Hz display.

## Scope

The design preserves:

- View-family scheduling and per-view page-table slices.
- The render-system-owned global physical page pool.
- Receiver-depth request marking.
- Page hit resolution, allocation, invalidation, caching, statistics, and finalization.
- The current four-level `[FirstLevel, LastLevel]` clipmap configuration.
- Single-sample shadow lookup without PCF or page gutters.
- Existing fail-fast error handling.

The design removes or does not reintroduce:

- Physical-page instances for shadow-caster draws.
- Dirty physical-page lists.
- Fixed per-caster page slices.
- Caster/page overlap compute culling.
- GPU-generated indirect draw arguments.
- `DrawInstancedIndirect` and `DrawIndexedInstancedIndirect`.
- Depth-attachment rendering for the physical shadow atlas.

Metal does not implement the new path in this milestone and must fail explicitly if it is used.

## Selected Approach

The physical atlas is an `R32Uint` sampled storage texture. Shadow rasterization uses an attachmentless graphics pass with the atlas bound as a pixel-shader UAV. Each fragment converts its virtual clipmap pixel into a logical page-table lookup, validates the resulting physical page, and atomically updates the physical atlas with `InterlockedMax`.

Two rejected alternatives are:

- A raw byte-address buffer atlas, which has weaker two-dimensional resource semantics and sampling locality.
- A fragment hit-list followed by compute resolution, which requires a large intermediate buffer, compaction, and an extra pass.

## Frame Flow

The per-family and per-view ordering remains:

1. Clear scene/request state.
2. Clear each View's request state.
3. Mark logical page requests from receiver depth.
4. Resolve page hits and recover cached mappings.
5. Allocate missing physical pages and mark pages as requested and dirty.
6. Clear dirty physical atlas pages with a fixed compute dispatch.
7. Rasterize shadow casters into virtual clipmap space and route fragments into the physical atlas.
8. Mark rendered pages and update redraw statistics.
9. Finalize the shared physical page pool.
10. Read back statistics.

For each View, allocation, clear, raster, and mark-rendered form one contiguous producer-consumer chain. This ordering prevents a later View from modifying shared physical resources before the earlier View has completed its writes.

## Physical Atlas

The physical atlas changes from:

```text
Depth32Float + DepthStencil + Sampled + comparison sampler
```

to:

```text
R32Uint + Storage + Sampled
```

The atlas has no depth-stencil view and no comparison sampler. Its size, physical page dimensions, physical slot mapping, and global ownership remain unchanged.

Atlas value `0` means no shadow caster has written the pixel.

## Dirty Page Clear

The clear pass is a compute pass with one thread group per physical page. It dispatches the fixed physical page capacity rather than consuming a GPU-generated list.

Each group:

1. Loads its `PhysicalPage` record.
2. Exits when the page is invalid, belongs to another View, is not requested, or is not dirty.
3. Cooperatively clears the page's `128 x 128` atlas region to zero.

An `8 x 8` group loops over the page in strides of eight pixels. With the current maximum 1024 physical pages, clean pages execute only one metadata test per thread and exit.

## Caster Rasterization

Each shadow caster produces one direct draw call:

```text
instanceCount = active clipmap level count
```

`SV_InstanceID` identifies the clipmap level. There is no instance per physical page.

The raster pass:

- Has no color attachment.
- Has no depth attachment.
- Uses a `16384 x 16384` viewport matching the virtual resolution.
- Binds the physical atlas as a fragment UAV.
- Binds the View's page-table slice and shared physical page metadata as read-only buffers.

The vertex shader transforms each mesh vertex into light space and projects it into the selected clipmap level's virtual working region. Geometry outside that level's virtual extent is clipped by normal rasterization.

## Fragment Page Routing

The pixel shader derives the logical page from `SV_Position.xy`:

```text
virtualPixel  = uint2(SV_Position.xy)
localPage     = virtualPixel / 128
pagePixel     = virtualPixel % 128
logicalIndex  = level * 16384 + localPage.y * 128 + localPage.x
```

The shader then:

1. Loads the dense page-table entry.
2. Discards an unmapped entry.
3. Converts the entry to a physical page index and checks capacity.
4. Loads physical page metadata.
5. Verifies valid, requested, and dirty flags.
6. Verifies the physical page ViewID and clipmap level.
7. Computes the physical slot origin.
8. Atomically writes the encoded shadow depth to `slotOrigin + pagePixel`.

The metadata validation prevents writes to clean cache entries or pages belonging to another View.

## Depth Encoding And Sampling

Normalized light depth remains in `[0, 1]`, with smaller values closer to the light.

The raster shader computes:

```text
reversedDepth = max(1 - normalizedDepth, minimumPositiveEncoding)
encodedDepth  = asuint(reversedDepth)
```

Positive IEEE-754 floats preserve numerical ordering in their unsigned bit representation. `InterlockedMax` therefore retains the largest reversed depth, which corresponds to the nearest caster. The minimum positive clamp keeps valid far-plane depth distinct from the empty value `0`.

Forward sampling uses:

```hlsl
uint encodedDepth = VirtualShadowAtlas.Load(int3(physicalPixel, 0));
if (encodedDepth == 0u)
{
    return 1.0f;
}

float casterDepth = 1.0f - asfloat(encodedDepth);
return receiverDepth - depthBias <= casterDepth ? 1.0f : 0.0f;
```

Normal bias remains applied to the receiver world position before resolving the page. PCF and gutters remain absent.

## Common RHI Changes

Add:

- `RhiFormat::R32Uint`.
- `RhiTextureUsage::Storage`.
- `RhiPipelineResourceKind::ReadWriteStorageTexture`.
- `RhiCommandList::SetReadWriteStorageTexture`.
- Texture support in UAV barrier recording.

Graphics pipelines and native render passes may have no color or depth attachment when they contain a fragment-stage read-write storage texture binding. An attachmentless pass without any fragment output remains invalid.

## D3D11 Backend

D3D11 creates the atlas with:

- `DXGI_FORMAT_R32_UINT`.
- `D3D11_BIND_SHADER_RESOURCE`.
- `D3D11_BIND_UNORDERED_ACCESS`.

The backend creates one SRV and one UAV for the texture.

Compute binding uses `CSSetUnorderedAccessViews`. Fragment binding uses `OMSetRenderTargetsAndUnorderedAccessViews`, with no render target or depth-stencil view for the shadow raster pass. Pipeline changes and pass completion unbind both compute and output-merger UAVs before the texture is sampled.

## D3D12 Backend

D3D12 creates the atlas with `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS` and creates matching SRV and UAV descriptors.

`ReadWriteStorageTexture` uses a UAV descriptor table. Texture UAVs are not represented as root UAV descriptors.

Resource state transitions are:

```text
NON_PIXEL_SHADER_RESOURCE -> UNORDERED_ACCESS  // compute clear
UNORDERED_ACCESS          -> UNORDERED_ACCESS  // raster, with UAV ordering
UNORDERED_ACCESS          -> PIXEL_SHADER_RESOURCE // forward sampling
```

The clear-to-raster boundary includes a UAV barrier so fragment atomics observe the completed clears.

## FrameGraph Changes

FrameGraph gains read-write storage texture access for compute and raster passes.

The dependency model records:

- Clear pass: physical pages read, atlas read-write storage.
- Raster pass: page table read, physical pages read, atlas read-write storage.
- Mark-rendered pass: atlas read for ordering, physical pages/statistics write.
- Forward passes: atlas sampled read.

Raster passes may request texture UAV ordering. Existing attachment validation remains unchanged for ordinary color/depth raster passes.

Every atlas write produces a new FrameGraph texture version, including writes by successive Views.

## Resource And Sampling API Cleanup

`VirtualShadowViewResult` and `VirtualShadowSamplingSnapshot` retain the atlas pointer and page-table range but remove the comparison sampler.

`VirtualShadowSceneCache` no longer creates or owns a comparison sampler for VSM.

`BindVirtualShadowSampling` binds:

- `Texture2D<uint>` atlas.
- The View's page-table slice.
- VSM constants.

No sampler is bound for the atlas because lookup uses integer `Load`.

## Failure Policy

The implementation terminates with a clear error when:

- R32Uint sampled-storage texture creation fails.
- An SRV or UAV cannot be created.
- An attachmentless fragment-UAV pipeline cannot be created.
- A declared storage texture cannot be bound at the requested stage.
- FrameGraph detects an undeclared texture access or invalid UAV ordering request.
- Metal reaches the unsupported storage-texture raster path.

Shader-side invalid page-table entries or stale metadata are discarded and never redirected to another physical page.

There is no depth-atlas fallback, indirect fallback, placeholder resource, or transaction mechanism.

## Verification

Before production implementation, one-off probes establish failing baselines for:

- R32Uint sampled-storage texture creation.
- Compute texture UAV binding.
- Attachmentless fragment texture UAV binding.
- FrameGraph read-write texture dependency validation.

The probes are removed after the permanent implementation is verified.

Permanent verification consists of:

- Strict FXC and DXC compilation of the clear compute shader and raster vertex/pixel shaders.
- Full Windows Debug build.
- Existing CTest suite.
- D3D11 Editor startup/render smoke.
- D3D12 Editor startup/render smoke.
- Manual Play validation of shadow correctness, page cache preservation, View isolation, and statistics.

Performance acceptance requires:

- One direct draw per caster with four clipmap instances in the current configuration.
- No draw or instance count proportional to physical page capacity.
- No caster/page culling compute pass.
- SampleScene Play remains stable at 75 FPS on the user's 75 Hz display and is not slower than `06c2911`.
