# Windows DX12 Startup And Editor Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep Windows applications on D3D11 by default, make `--dx12` select D3D12 consistently for both Player and Editor, and render the complete Editor, Scene View, and Game View through D3D12.

**Architecture:** One Win32 platform helper parses the shared backend switch for both executable entry points. The D3D12 device owns a shared shader-visible SRV heap and descriptor allocator used by both engine textures and Dear ImGui; narrow opaque RHI hooks expose the allocator, swapchain metadata, and active native command list to the Windows Editor integration.

**Tech Stack:** C++20, Win32, D3D11, D3D12, DXGI, Dear ImGui 1.92.8, CMake, MSVC, CTest.

---

## File Map

- `Engine/RHI/Common/RhiDevice.h`: define the opaque native shader-resource descriptor allocation contract, expose swapchain buffer count, and document D3D12 sampled-view handles.
- `Engine/RHI/D3D11/D3D11Rhi.cpp`: report the D3D11 swapchain buffer count required by the common interface.
- `Engine/RHI/D3D12/D3D12Rhi.cpp`: implement the shared shader-visible heap, allocate texture SRVs from it, return GPU descriptor handles, expose the native command list, and transition sampled render targets after a render pass.
- `Engine/RHI/Metal/MetalRhi.mm`: report Metal's effective drawable count required by the common swapchain interface.
- `Engine/Runtime/Render/RenderSystem.h/.cpp`: forward swapchain format/count and the native descriptor allocator through `RenderNativeHandles`.
- `Editor/Windows/WinEditorRenderBackend.h/.cpp`: dispatch Dear ImGui initialization, frame begin, rendering, and shutdown between DX11 and DX12.
- `Engine/Runtime/Platform/Windows/Win32RenderBackendSelection.h/.cpp`: parse the exact `--dx12` switch once for both Windows applications.
- `Editor/Windows/main.cpp`: select its backend through the shared Win32 helper.
- `Player/Windows/main.cpp`: select its backend through the shared Win32 helper.
- `CMake/Targets/Engine.cmake`: compile the shared Win32 helper and propagate its `shell32` dependency.
- `Docs/ArchitectureOverview.md`: document the Windows Editor and Player backend selection contract.

## Task 1: Add The Common Native Descriptor Bridge

**Files:**

- Modify: `Engine/RHI/Common/RhiDevice.h`
- Modify: `Engine/RHI/D3D11/D3D11Rhi.cpp`
- Modify: `Engine/RHI/Metal/MetalRhi.mm`
- Modify: `Engine/Runtime/Render/RenderSystem.h`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`

- [ ] **Step 1: Confirm the bridge is absent**

Run:

```powershell
rg -n "RhiNativeShaderResourceDescriptor|shaderResourceDescriptorAllocator|mainSwapchainBufferCount" Engine Editor
```

Expected: no matches.

- [ ] **Step 2: Define an opaque descriptor and allocator interface**

Add to `Engine/RHI/Common/RhiDevice.h` before `RhiDevice`:

```cpp
struct RhiNativeShaderResourceDescriptor
{
    uint64_t cpuHandle = 0;
    uint64_t gpuHandle = 0;
};

class RhiNativeShaderResourceDescriptorAllocator
{
public:
    virtual ~RhiNativeShaderResourceDescriptorAllocator() = default;

    [[nodiscard]] virtual void* GetNativeHeapHandle() const noexcept = 0;
    [[nodiscard]] virtual bool Allocate(RhiNativeShaderResourceDescriptor& outDescriptor) = 0;
    virtual void Release(RhiNativeShaderResourceDescriptor descriptor) noexcept = 0;
};
```

Add this default native hook to `RhiDevice`:

```cpp
[[nodiscard]] virtual RhiNativeShaderResourceDescriptorAllocator* GetNativeShaderResourceDescriptorAllocator() const noexcept
{
    return nullptr;
}
```

Add this pure virtual metadata accessor to `RhiSwapchain`:

```cpp
[[nodiscard]] virtual uint32_t GetBufferCount() const noexcept = 0;
```

Update `RhiTexture::GetNativeSampledViewHandle()` documentation to state that D3D12 returns a GPU descriptor handle encoded as the opaque pointer value.

- [ ] **Step 3: Implement swapchain buffer counts**

Store the effective count in `D3D11Swapchain` and return it:

```cpp
[[nodiscard]] uint32_t GetBufferCount() const noexcept override
{
    return bufferCount_;
}

uint32_t bufferCount_ = 2;
```

Pass `swapchainDesc.BufferCount` into the D3D11 swapchain constructor. Add the equivalent D3D12 getter returning its existing `bufferCount_`. Metal returns the configured drawable count used by the first-stage swapchain; use `3` consistently in its constructor and getter.

- [ ] **Step 4: Forward native integration data through RenderSystem**

Extend `RenderNativeHandles` in `RenderSystem.h`:

```cpp
UInt32 mainSwapchainBufferCount = 0;
rhi::RhiFormat mainSwapchainColorFormat = rhi::RhiFormat::Unknown;
rhi::RhiNativeShaderResourceDescriptorAllocator* shaderResourceDescriptorAllocator = nullptr;
```

In `RenderSystem::QueryNativeHandles`, populate the fields only from live RHI objects:

```cpp
outHandles.shaderResourceDescriptorAllocator = impl_->device->GetNativeShaderResourceDescriptorAllocator();
if (impl_->mainSwapchain != nullptr)
{
    outHandles.mainSwapchainBufferCount = impl_->mainSwapchain->GetBufferCount();
    outHandles.mainSwapchainColorFormat = impl_->mainSwapchain->GetColorFormat();
}
```

- [ ] **Step 5: Build the common interface changes**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngine
```

Expected: `VEngine` builds successfully; every concrete swapchain implements `GetBufferCount()`.

- [ ] **Step 6: Commit the common bridge**

```powershell
git add Engine/RHI/Common/RhiDevice.h Engine/RHI/D3D11/D3D11Rhi.cpp Engine/RHI/D3D12/D3D12Rhi.cpp Engine/RHI/Metal/MetalRhi.mm Engine/Runtime/Render/RenderSystem.h Engine/Runtime/Render/RenderSystem.cpp
git commit -m "rhi: expose editor descriptor integration"
```

## Task 2: Implement The D3D12 Shared SRV Heap

**Files:**

- Modify: `Engine/RHI/D3D12/D3D12Rhi.cpp`

- [ ] **Step 1: Capture the existing per-texture heap behavior**

Run:

```powershell
rg -n "CreateDescriptorHeap texture SRV|srvHeap_|GetNativeSampledViewHandle" Engine/RHI/D3D12/D3D12Rhi.cpp
```

Expected: each sampled texture creates its own one-descriptor shader-visible heap, and D3D12 does not override `GetNativeSampledViewHandle()`.

- [ ] **Step 2: Add a bounded shared allocator**

Add an internal `D3D12ShaderResourceDescriptorAllocator` implementing the common bridge. Use a capacity of 4096 descriptors, a monotonically increasing `nextIndex_`, an `allocatedSlots_` vector, a reusable `freeIndices_` vector, a fence-tagged `retiredIndices_` vector, and `std::mutex` around allocation/release. Its initialization creates one `D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV` heap with `D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE` and retains the device's internal fence. Allocation first moves retired slots whose fence value is complete into `freeIndices_`, then selects a free or new index and marks it allocated. It computes CPU/GPU handles from the heap starts and descriptor increment size:

```cpp
outDescriptor.cpuHandle = cpuStart_.ptr + (static_cast<uint64_t>(index) * descriptorSize_);
outDescriptor.gpuHandle = gpuStart_.ptr + (static_cast<uint64_t>(index) * descriptorSize_);
```

Release validates that both handles correspond to the same in-range allocated slot, marks it unallocated, and appends `{index, lastSubmittedFenceValue_}` to `retiredIndices_`. Invalid or duplicate releases trigger the project assertion facade rather than corrupting allocator state. Add `NotifySubmission(uint64_t fenceValue)` to update the latest fence associated with subsequent retirements.

- [ ] **Step 3: Initialize and expose the allocator from D3D12Device**

After `ID3D12Device` creation succeeds, construct and initialize the shared allocator. On failure, set:

```cpp
SetLastError("Failed to create the D3D12 shared shader-resource descriptor heap.");
return false;
```

Expose it with:

```cpp
[[nodiscard]] RhiNativeShaderResourceDescriptorAllocator* GetNativeShaderResourceDescriptorAllocator() const noexcept override
{
    return shaderResourceDescriptorAllocator_.get();
}
```

After each normal D3D12 queue submission, signal the device's existing internal fence with the next fence value and call `shaderResourceDescriptorAllocator_->NotifySubmission(fenceValue)`. Preserve the existing per-frame completion-fence signal. `WaitIdle()` continues using the same monotonically increasing internal fence sequence, so completed retired slots become reusable without stalling normal texture destruction.

- [ ] **Step 4: Move sampled textures onto the shared heap**

Replace `D3D12Texture`'s private SRV heap with a shared allocator plus an allocated descriptor. Its destructor releases a non-zero descriptor. These methods become:

```cpp
[[nodiscard]] ID3D12DescriptorHeap* GetShaderResourceViewHeap() const noexcept
{
    return static_cast<ID3D12DescriptorHeap*>(shaderResourceDescriptorAllocator_->GetNativeHeapHandle());
}

[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetShaderResourceView() const noexcept
{
    return D3D12_GPU_DESCRIPTOR_HANDLE{shaderResourceDescriptor_.gpuHandle};
}

[[nodiscard]] void* GetNativeSampledViewHandle() const noexcept override
{
    return reinterpret_cast<void*>(static_cast<uintptr_t>(shaderResourceDescriptor_.gpuHandle));
}
```

In `D3D12Device::CreateTexture`, call the shared allocator for sampled textures, report `"D3D12 shader-resource descriptor heap is exhausted."` on failure, create the SRV at `D3D12_CPU_DESCRIPTOR_HANDLE{descriptor.cpuHandle}`, and transfer the allocation to `D3D12Texture`.

- [ ] **Step 5: Expose the active native command list**

Replace the D3D12-only helper with the common override:

```cpp
[[nodiscard]] void* GetNativeCommandBufferHandle() const noexcept override
{
    return commandList_.Get();
}
```

Update the internal submission cast to obtain the command list through the concrete object without changing submission behavior.

- [ ] **Step 6: Make sampled render targets readable after their pass**

In `D3D12CommandList::EndRenderPass()`, before clearing `activeTexture_`, transition a texture with an SRV from its current render-target state to `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE` and update its tracked state:

```cpp
if (activeTexture_ != nullptr && activeTexture_->HasShaderResourceView())
{
    TransitionResource(activeTexture_->GetNativeResource(), activeTexture_->GetResourceState(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    activeTexture_->SetResourceState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
```

This lets the following Editor overlay pass sample Scene View and Game View without bypassing RHI ownership.

- [ ] **Step 7: Build and run existing RHI coverage**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngine
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R "Rhi|Render" --output-on-failure
```

Expected: the library builds and all selected existing tests pass.

- [ ] **Step 8: Commit the D3D12 descriptor work**

```powershell
git add Engine/RHI/D3D12/D3D12Rhi.cpp
git commit -m "rhi: share D3D12 shader resource descriptors"
```

## Task 3: Add Dear ImGui DX12 Rendering

**Files:**

- Modify: `Editor/Windows/WinEditorRenderBackend.h`
- Modify: `Editor/Windows/WinEditorRenderBackend.cpp`

- [ ] **Step 1: Confirm the Editor rejects D3D12**

Run:

```powershell
rg -n "currently supports ImGui rendering through the D3D11 backend|ImGui_ImplDX11" Editor/Windows/WinEditorRenderBackend.cpp
```

Expected: the backend returns `Unsupported` for every backend except D3D11.

- [ ] **Step 2: Store only the state required for dispatch**

Replace the unused native-device member with the common allocator pointer:

```cpp
rhi::RhiNativeShaderResourceDescriptorAllocator* shaderResourceDescriptorAllocator_ = nullptr;
bool initialized_ = false;
```

Add private `InitD3D11`, `InitD3D12`, `ShutdownD3D11`, and `ShutdownD3D12` helpers so public lifecycle methods only switch on `backend_`.

- [ ] **Step 3: Initialize ImGui DX12 from RenderNativeHandles**

Include `imgui_impl_dx12.h` and validate device, graphics queue, allocator, native heap, buffer count, and format. Convert `RhiFormat::Bgra8Unorm` to `DXGI_FORMAT_B8G8R8A8_UNORM`; return `Unsupported` for other formats.

Build `ImGui_ImplDX12_InitInfo`:

```cpp
ImGui_ImplDX12_InitInfo initInfo = {};
initInfo.Device = nativeDevice;
initInfo.CommandQueue = nativeQueue;
initInfo.NumFramesInFlight = static_cast<int>(nativeHandles.mainSwapchainBufferCount);
initInfo.RTVFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
initInfo.UserData = nativeHandles.shaderResourceDescriptorAllocator;
initInfo.SrvDescriptorHeap = nativeHeap;
initInfo.SrvDescriptorAllocFn = AllocateImGuiDescriptor;
initInfo.SrvDescriptorFreeFn = ReleaseImGuiDescriptor;
```

The callbacks cast `UserData` back to the common allocator and translate between `uint64_t` and D3D12 CPU/GPU descriptor handles. Return `PlatformError` if `ImGui_ImplDX12_Init(&initInfo)` fails.

- [ ] **Step 4: Dispatch frame, draw, and shutdown operations**

Use `ImGui_ImplDX12_NewFrame()` for D3D12. During draw, obtain the native command list through `commandList.GetNativeCommandBufferHandle()`, validate it, and call:

```cpp
ImGui_ImplDX12_RenderDrawData(&drawData, nativeCommandList);
```

Call `ImGui_ImplDX12_Shutdown()` for D3D12 and clear `shaderResourceDescriptorAllocator_`. Preserve the existing DX11 behavior unchanged.

- [ ] **Step 5: Build the Editor integration**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor
```

Expected: `VEngineWinEditor.exe` links both ImGui renderer backends successfully.

- [ ] **Step 6: Commit the Editor renderer integration**

```powershell
git add Editor/Windows/WinEditorRenderBackend.h Editor/Windows/WinEditorRenderBackend.cpp
git commit -m "editor: render ImGui through D3D12"
```

## Task 4: Add The Shared `--dx12` Startup Switch

**Files:**

- Create: `Engine/Runtime/Platform/Windows/Win32RenderBackendSelection.h`
- Create: `Engine/Runtime/Platform/Windows/Win32RenderBackendSelection.cpp`
- Modify: `Editor/Windows/main.cpp`
- Modify: `Player/Windows/main.cpp`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Add the shared Win32 backend selector**

Declare a platform helper without exposing Windows SDK types:

```cpp
#pragma once

namespace ve
{
    enum class RenderBackend;

    [[nodiscard]] RenderBackend SelectWin32RenderBackendFromCommandLine();
} // namespace ve
```

In the `.cpp`, include `RenderSystem.h`, `Windows.h`, `<shellapi.h>`, and `<string_view>`. Implement an exact, case-sensitive scan that always defaults to D3D11:

```cpp
ve::RenderBackend ve::SelectWin32RenderBackendFromCommandLine()
{
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr)
    {
        return RenderBackend::D3D11;
    }

    RenderBackend backend = RenderBackend::D3D11;
    for (int argumentIndex = 1; argumentIndex < argumentCount; ++argumentIndex)
    {
        if (std::wstring_view(arguments[argumentIndex]) == L"--dx12")
        {
            backend = RenderBackend::D3D12;
            break;
        }
    }

    LocalFree(arguments);
    return backend;
}
```

- [ ] **Step 2: Use the helper from Editor and Player**

Include the new header in both Windows `main.cpp` files and replace their fixed assignments with:

```cpp
initParam.runtime.renderSystem.device.backend = ve::SelectWin32RenderBackendFromCommandLine();
```

Keep all other initialization unchanged.

- [ ] **Step 3: Add the shared helper to the Windows engine target**

Add both files to the `WIN32` `target_sources(VEngine)` block and propagate `shell32` with the existing Windows SDK libraries:

```cmake
target_link_libraries(VEngine
    PUBLIC
        user32
        shell32
)
```

- [ ] **Step 4: Build both Windows executables**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor VEngineWinPlayer
```

Expected: both executables build with no compile or link errors and share one command-line parser implementation.

- [ ] **Step 5: Commit startup selection**

```powershell
git add Engine/Runtime/Platform/Windows/Win32RenderBackendSelection.h Engine/Runtime/Platform/Windows/Win32RenderBackendSelection.cpp Editor/Windows/main.cpp Player/Windows/main.cpp CMake/Targets/Engine.cmake
git commit -m "application: add shared D3D12 startup mode"
```

## Task 5: Document And Verify Both Backends

**Files:**

- Modify: `Docs/ArchitectureOverview.md`

- [ ] **Step 1: Document the stable selection contract**

In the Windows Editor target section, state:

```markdown
The Windows Editor and Player use D3D11 by default. Launching either executable with `--dx12` selects the D3D12 RHI; the Editor also selects the Dear ImGui D3D12 renderer. An explicitly requested D3D12 startup failure is reported without falling back to D3D11.
```

- [ ] **Step 2: Run formatting and repository checks**

Run:

```powershell
git diff --check
rg -n "currently supports ImGui rendering through the D3D11 backend" Editor
```

Expected: `git diff --check` succeeds and the obsolete D3D11-only warning has no matches.

- [ ] **Step 3: Build and run the full Windows test preset**

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
```

Expected: the full debug build succeeds and all registered tests pass.

- [ ] **Step 4: Smoke-test D3D11 mode**

Launch `VEngineWinEditor.exe` from the preset output directory. Verify the title reports D3D11, the main UI renders, Scene View and Game View display, and shutdown is clean.

- [ ] **Step 5: Smoke-test D3D12 mode**

Launch `VEngineWinEditor.exe --dx12`. Verify the title reports D3D12, the main UI renders, Scene View and Game View display, both viewport panels survive repeated resizing, and shutdown produces no D3D12 descriptor or resource-state validation errors.

- [ ] **Step 6: Smoke-test Player D3D11 mode**

Launch `VEngineWinPlayer.exe`. Verify startup initializes D3D11 in the log, the normal scene/render path presents frames, and shutdown is clean.

- [ ] **Step 7: Smoke-test Player D3D12 mode**

Launch `VEngineWinPlayer.exe --dx12`. Verify startup initializes D3D12 in the log, the same scene/render path presents frames, and shutdown produces no D3D12 validation errors.

- [ ] **Step 8: Commit documentation**

```powershell
git add Docs/ArchitectureOverview.md
git commit -m "docs: document Windows application backend selection"
```

- [ ] **Step 9: Review the final change set**

Run:

```powershell
git status --short
git log --oneline -6
git diff HEAD~5 --stat
```

Expected: the worktree is clean, the feature is represented by focused commits, and changes are limited to the RHI bridge, D3D12 descriptor implementation, Windows Editor integration, shared Windows startup selection, and documentation.
