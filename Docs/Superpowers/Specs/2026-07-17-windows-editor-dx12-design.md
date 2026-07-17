# Windows Editor DX12 Compatibility Design

## Goal

Keep D3D11 as the default Windows Editor render backend and add an explicit `VEngineWinEditor.exe --dx12` startup mode that renders the complete Editor, including Scene View and Game View textures, through D3D12.

## Scope

This change covers Windows Editor backend selection, Dear ImGui D3D12 integration, the D3D12 command-list bridge, and D3D12 shader-resource descriptor handling required by editor viewport textures.

The following remain outside this change:

- Changing the Windows Player backend selection.
- Persisting the selected backend in project or user settings.
- Automatically falling back from an explicitly requested D3D12 backend to D3D11.
- Adding new rendering features or redesigning the common RHI.

## Startup Selection

`Editor/Windows/main.cpp` parses the process command line with the Win32 wide-character argument API.

- With no `--dx12` argument, the Editor selects `RenderBackend::D3D11` exactly as it does today.
- With an exact, case-sensitive `--dx12` argument, the Editor selects `RenderBackend::D3D12`.
- Other arguments do not change backend selection.

The window title continues to display the active backend through the existing Editor backend-name path. If explicitly requested D3D12 initialization fails, startup reports the existing concrete initialization error and exits. It does not silently create a D3D11 device.

## D3D12 Shader-Resource Descriptor Ownership

Dear ImGui's D3D12 renderer requires every texture referenced by its draw data to reside in the shader-visible descriptor heap bound for the draw. The current D3D12 RHI creates a separate SRV heap for each texture, which cannot support a single ImGui draw pass containing the font atlas, Scene View, and Game View textures.

The D3D12 device will therefore own one bounded shader-visible CBV/SRV/UAV descriptor heap and a small descriptor-slot allocator. D3D12 textures with `RhiTextureUsage::ShaderRead` allocate one slot from this heap, create their SRV in that slot, and release the slot when the texture is destroyed. `D3D12Texture::GetNativeSampledViewHandle()` returns the slot's GPU descriptor handle encoded through the existing opaque native sampled-view contract.

The allocator is owned by the D3D12 device and shared with textures so allocator state outlives every texture allocation. Allocation failure makes texture creation fail with a precise backend error. Slot allocation and release occur on the Render Thread under the existing RHI ownership rule; the allocator still protects its free-list state so Dear ImGui's descriptor callbacks have an explicit safety boundary.

The initial heap capacity is a fixed engine constant sized for the current lightweight runtime and editor. Capacity is not exposed as public configuration in this slice.

## Native Integration Boundary

The common RHI keeps native objects opaque. Two narrow accessors are added for platform integrations:

- `RhiCommandList::GetNativeCommandBufferHandle()` returns the active `ID3D12GraphicsCommandList*` from the D3D12 implementation.
- `RhiDevice` exposes the native shader-resource descriptor heap plus opaque descriptor allocation and release operations needed by a renderer integration such as Dear ImGui.

D3D11 and Metal preserve their existing behavior; unsupported native descriptor operations return null or failure. `RenderNativeHandles` forwards the D3D12 descriptor integration handles when `RenderSystem::QueryNativeHandles()` runs on the Render Thread. No D3D12 header enters a common RHI header.

## Windows Editor Render Backend

`WinEditorRenderBackend` becomes a backend-dispatching integration rather than a D3D11-only implementation.

For D3D11 it retains the existing flow:

1. Initialize `imgui_impl_dx11` with the native device and immediate context.
2. Begin each UI frame with `ImGui_ImplDX11_NewFrame()`.
3. Render draw data through the D3D11 immediate context.
4. Shut down `imgui_impl_dx11`.

For D3D12 it performs the parallel flow:

1. Query the native D3D12 device, graphics queue, shared shader-resource heap, descriptor allocation bridge, swapchain buffer count, and swapchain color format.
2. Initialize `imgui_impl_dx12` with `ImGui_ImplDX12_InitInfo` and allocator callbacks that delegate to the RHI descriptor bridge.
3. Begin each UI frame with `ImGui_ImplDX12_NewFrame()`.
4. Obtain `ID3D12GraphicsCommandList*` from the RHI command list and call `ImGui_ImplDX12_RenderDrawData()` while the existing Editor overlay render pass is active.
5. Shut down `imgui_impl_dx12` before the RHI device and descriptor heap are destroyed.

Initialization validates every required native handle. A missing handle, descriptor allocation failure, or ImGui initialization failure returns a concrete existing `ErrorCode` and logs enough backend context to diagnose startup.

## Frame Data Flow

The existing Editor frame pipeline remains authoritative:

1. Scene Thread builds the Editor UI and captures copied ImGui draw data.
2. Render Thread records Scene View and Game View rendering into their render textures.
3. The Editor overlay frame-graph pass opens the main swapchain render pass.
4. `WinEditorRenderBackend` records ImGui DX11 or DX12 commands into that pass.
5. The existing RHI submission and presentation path submits and presents the frame.

DX12 Scene View and Game View `ImTextureID` values are GPU descriptor handles from the shared shader-visible heap. Their render textures remain alive through the existing `KeepImGuiTextureAlive` mechanism, so copied ImGui draw data cannot outlive the descriptor-producing texture.

## Resource Lifetime And Failure Behavior

The D3D12 descriptor heap is created during D3D12 device initialization and destroyed with the device after the queue is idle. Textures return slots before the allocator is released. ImGui releases its font and dynamic texture descriptors during Editor backend shutdown, which already occurs before `Application::UnInit()` destroys the render device.

If the descriptor heap is exhausted, the RHI records a backend error and refuses the resource creation or ImGui descriptor allocation. It does not return descriptor zero or reuse a live slot. DX11 behavior is unchanged.

## Verification

Repository guidance does not request new unit tests for this feature. Verification uses the existing Windows build and smoke-test infrastructure:

- Configure and build with both D3D11 and D3D12 enabled through `CMake/Scripts/WithMsvc.bat`.
- Run the existing Windows CTest preset, including RHI smoke coverage.
- Launch `VEngineWinEditor.exe` and verify the title reports D3D11, the UI renders, and Scene View and Game View display.
- Launch `VEngineWinEditor.exe --dx12` and verify the title reports D3D12, the UI renders, and Scene View and Game View display.
- Resize both viewport panels in D3D12 mode to exercise descriptor release and reallocation.
- Close both modes cleanly and check the log and D3D12 debug layer for descriptor, resource-state, or lifetime errors.

## Acceptance Criteria

- The Windows Editor still defaults to D3D11.
- `--dx12` selects D3D12 without changing unrelated arguments or Player behavior.
- The full Editor UI, Scene View, and Game View render correctly in both backends.
- D3D12 ImGui commands use the Render Thread's existing RHI command list and submission path.
- D3D12 viewport texture descriptors and ImGui-managed descriptors come from one shader-visible heap.
- Explicit D3D12 startup failures are visible and never hidden by an automatic D3D11 fallback.
- Existing Windows build and test presets pass.
