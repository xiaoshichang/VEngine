#pragma once

#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Scene/OSEventQueue.h"

#include <atomic>

namespace ve::editor
{
    /// Owns editor-level lifecycle and SceneThread callbacks.
    class Editor : public NonMovable
    {
    public:
        Editor() = default;
        ~Editor();

        [[nodiscard]] ErrorCode Init(EngineRuntime& runtime, void* nativeWindowHandle);
        void StartFrame();
        [[nodiscard]] bool OnOSEvent(const OSEvent& event);
        void Render();
        void UnInit() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        [[nodiscard]] ErrorCode InitRenderBackend(RenderSystem& renderSystem);
        void ShutdownRenderBackend() noexcept;

        SceneSystem* sceneSystem_ = nullptr;
        RenderSystem* renderSystem_ = nullptr;
        RenderBackend renderBackend_ = RenderBackend::D3D12;
        std::atomic_bool initialized_{false};
        bool showDemoWindow_ = true;
    };
} // namespace ve::editor
