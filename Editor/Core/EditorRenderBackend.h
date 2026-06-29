#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/RenderSystem.h"

#include <memory>

struct ImDrawData;

namespace ve::rhi
{
    class RhiCommandList;
}

namespace ve::editor
{
    /// Owns the platform-specific editor UI render backend integration.
    class EditorRenderBackend : public NonMovable
    {
    public:
        virtual ~EditorRenderBackend() = default;

        [[nodiscard]] virtual ErrorCode Init(RenderSystem& renderSystem) = 0;
        virtual void BeginFrame() = 0;
        virtual void Shutdown() noexcept = 0;
        virtual void RenderDrawData(rhi::RhiCommandList& commandList, ImDrawData& drawData) = 0;

        [[nodiscard]] RenderBackend GetBackend() const noexcept
        {
            return backend_;
        }

    protected:
        RenderBackend backend_ = RenderBackend::D3D12;
    };

    [[nodiscard]] std::unique_ptr<EditorRenderBackend> CreateWinEditorRenderBackend();
    [[nodiscard]] std::unique_ptr<EditorRenderBackend> CreateMacEditorRenderBackend();
} // namespace ve::editor
