#include "Editor/Windows/WinEditorRenderBackend.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_dx12.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef GetMessage
#undef GetMessage
#endif
#include <d3d11.h>
#include <d3d12.h>

#include <memory>

namespace ve::editor
{
    namespace
    {
        void AllocateImGuiDescriptor(ImGui_ImplDX12_InitInfo* initInfo,
                                     D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescriptor,
                                     D3D12_GPU_DESCRIPTOR_HANDLE* outGpuDescriptor)
        {
            VE_ASSERT(initInfo != nullptr);
            VE_ASSERT(outCpuDescriptor != nullptr);
            VE_ASSERT(outGpuDescriptor != nullptr);
            auto* allocator = static_cast<rhi::RhiNativeShaderResourceDescriptorAllocator*>(initInfo->UserData);
            VE_ASSERT(allocator != nullptr);

            rhi::RhiNativeShaderResourceDescriptor descriptor = {};
            if (!allocator->Allocate(descriptor))
            {
                *outCpuDescriptor = {};
                *outGpuDescriptor = {};
                VE_LOG_ERROR_CATEGORY("Editor", "Dear ImGui failed to allocate a D3D12 shader-resource descriptor.");
                VE_ASSERT_ALWAYS_MESSAGE(false, "Dear ImGui exhausted the D3D12 shader-resource descriptor heap.");
                return;
            }

            outCpuDescriptor->ptr = descriptor.cpuHandle;
            outGpuDescriptor->ptr = descriptor.gpuHandle;
        }

        void ReleaseImGuiDescriptor(ImGui_ImplDX12_InitInfo* initInfo,
                                    D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor,
                                    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor)
        {
            VE_ASSERT(initInfo != nullptr);
            auto* allocator = static_cast<rhi::RhiNativeShaderResourceDescriptorAllocator*>(initInfo->UserData);
            VE_ASSERT(allocator != nullptr);
            allocator->Release(rhi::RhiNativeShaderResourceDescriptor{cpuDescriptor.ptr, gpuDescriptor.ptr});
        }
    } // namespace

    ErrorCode WinEditorRenderBackend::Init(RenderSystem& renderSystem)
    {
        RenderNativeHandles nativeHandles;
        const ErrorCode queryResult = renderSystem.QueryNativeHandles(nativeHandles);
        if (queryResult != ErrorCode::None)
        {
            return queryResult;
        }

        if (!nativeHandles.hasMainSwapchain)
        {
            return ErrorCode::InvalidState;
        }

        backend_ = nativeHandles.backend;
        ErrorCode initResult = ErrorCode::Unsupported;
        switch (backend_)
        {
        case RenderBackend::D3D11:
            initResult = InitD3D11(nativeHandles);
            break;
        case RenderBackend::D3D12:
            initResult = InitD3D12(nativeHandles);
            break;
        case RenderBackend::Metal:
            break;
        }

        if (initResult != ErrorCode::None)
        {
            return initResult;
        }

        initialized_ = true;
        return ErrorCode::None;
    }

    ErrorCode WinEditorRenderBackend::InitD3D11(const RenderNativeHandles& nativeHandles)
    {
        auto* nativeDevice = static_cast<ID3D11Device*>(nativeHandles.device);
        auto* nativeContext = static_cast<ID3D11DeviceContext*>(nativeHandles.immediateContext);
        if (nativeDevice == nullptr || nativeContext == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        if (!ImGui_ImplDX11_Init(nativeDevice, nativeContext))
        {
            return ErrorCode::PlatformError;
        }

        return ErrorCode::None;
    }

    ErrorCode WinEditorRenderBackend::InitD3D12(const RenderNativeHandles& nativeHandles)
    {
        auto* nativeDevice = static_cast<ID3D12Device*>(nativeHandles.device);
        auto* nativeQueue = static_cast<ID3D12CommandQueue*>(nativeHandles.graphicsQueue);
        auto* descriptorAllocator = nativeHandles.shaderResourceDescriptorAllocator;
        auto* descriptorHeap = descriptorAllocator != nullptr ? static_cast<ID3D12DescriptorHeap*>(descriptorAllocator->GetNativeHeapHandle()) : nullptr;
        if (nativeDevice == nullptr || nativeQueue == nullptr || descriptorAllocator == nullptr || descriptorHeap == nullptr ||
            nativeHandles.mainSwapchainBufferCount == 0)
        {
            return ErrorCode::InvalidState;
        }

        if (nativeHandles.mainSwapchainColorFormat != rhi::RhiFormat::Bgra8Unorm)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Dear ImGui D3D12 integration requires a BGRA8 main swapchain.");
            return ErrorCode::Unsupported;
        }

        ImGui_ImplDX12_InitInfo initInfo = {};
        initInfo.Device = nativeDevice;
        initInfo.CommandQueue = nativeQueue;
        initInfo.NumFramesInFlight = static_cast<int>(nativeHandles.mainSwapchainBufferCount);
        initInfo.RTVFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
        initInfo.UserData = descriptorAllocator;
        initInfo.SrvDescriptorHeap = descriptorHeap;
        initInfo.SrvDescriptorAllocFn = AllocateImGuiDescriptor;
        initInfo.SrvDescriptorFreeFn = ReleaseImGuiDescriptor;
        if (!ImGui_ImplDX12_Init(&initInfo))
        {
            return ErrorCode::PlatformError;
        }

        shaderResourceDescriptorAllocator_ = descriptorAllocator;
        return ErrorCode::None;
    }

    void WinEditorRenderBackend::BeginFrame()
    {
        if (!initialized_)
        {
            return;
        }

        switch (backend_)
        {
        case RenderBackend::D3D11:
            ImGui_ImplDX11_NewFrame();
            break;
        case RenderBackend::D3D12:
            ImGui_ImplDX12_NewFrame();
            break;
        case RenderBackend::Metal:
            break;
        }
    }

    void WinEditorRenderBackend::Shutdown() noexcept
    {
        if (!initialized_)
        {
            return;
        }

        VE_ASSERT_MESSAGE(ImGui::GetCurrentContext() != nullptr, "WinEditorRenderBackend::Shutdown requires an active ImGui context.");
        switch (backend_)
        {
        case RenderBackend::D3D11:
            ShutdownD3D11();
            break;
        case RenderBackend::D3D12:
            ShutdownD3D12();
            break;
        case RenderBackend::Metal:
            break;
        }

        initialized_ = false;
    }

    void WinEditorRenderBackend::ShutdownD3D11() noexcept
    {
        ImGui_ImplDX11_Shutdown();
    }

    void WinEditorRenderBackend::ShutdownD3D12() noexcept
    {
        ImGui_ImplDX12_Shutdown();
        shaderResourceDescriptorAllocator_ = nullptr;
    }

    void WinEditorRenderBackend::RenderDrawData(rhi::RhiCommandList& commandList, ImDrawData& drawData)
    {
        if (!initialized_)
        {
            return;
        }

        switch (backend_)
        {
        case RenderBackend::D3D11:
            ImGui_ImplDX11_RenderDrawData(&drawData);
            break;
        case RenderBackend::D3D12:
        {
            auto* nativeCommandList = static_cast<ID3D12GraphicsCommandList*>(commandList.GetNativeCommandBufferHandle());
            VE_ASSERT_MESSAGE(nativeCommandList != nullptr, "Dear ImGui D3D12 rendering requires a native graphics command list.");
            if (nativeCommandList != nullptr)
            {
                ImGui_ImplDX12_RenderDrawData(&drawData, nativeCommandList);
            }
            break;
        }
        case RenderBackend::Metal:
            break;
        }
    }

    std::unique_ptr<EditorRenderBackend> CreateWinEditorRenderBackend()
    {
        return std::make_unique<WinEditorRenderBackend>();
    }
} // namespace ve::editor
