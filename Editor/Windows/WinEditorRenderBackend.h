#pragma once

#include "Editor/Core/EditorRenderBackend.h"

struct ID3D12DescriptorHeap;

namespace ve::editor
{
    class WinEditorRenderBackend final : public EditorRenderBackend
    {
    public:
        [[nodiscard]] ErrorCode Init(RenderSystem& renderSystem) override;
        void BeginFrame() override;
        void Shutdown() noexcept override;
        void RenderDrawData(rhi::RhiCommandList& commandList, ImDrawData& drawData) override;

    private:
        [[nodiscard]] ErrorCode InitD3D11(const RenderNativeHandles& nativeHandles);
        [[nodiscard]] ErrorCode InitD3D12(const RenderNativeHandles& nativeHandles);
        void ShutdownD3D11() noexcept;
        void ShutdownD3D12() noexcept;

        rhi::RhiNativeShaderResourceDescriptorAllocator* shaderResourceDescriptorAllocator_ = nullptr;
        ID3D12DescriptorHeap* shaderResourceDescriptorHeap_ = nullptr;
        bool initialized_ = false;
    };
} // namespace ve::editor
