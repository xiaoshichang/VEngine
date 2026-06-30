#pragma once

#include "Editor/Core/EditorRenderBackend.h"

namespace ve::editor
{
    class MacEditorRenderBackend final : public EditorRenderBackend
    {
    public:
        [[nodiscard]] ErrorCode Init(RenderSystem& renderSystem) override;
        void BeginFrame() override;
        void Shutdown() noexcept override;
        void RenderDrawData(rhi::RhiCommandList& commandList, ImDrawData& drawData) override;

    private:
        void* nativeDevice_ = nullptr;
        void* imguiFramebufferTexture_ = nullptr;
        bool initialized_ = false;
    };
} // namespace ve::editor
