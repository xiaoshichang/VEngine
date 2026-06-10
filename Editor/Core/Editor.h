#pragma once

#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
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

        [[nodiscard]] ErrorCode Init(EngineRuntime& runtime);
        void OnOSEvent(const OSEvent& event);
        void Render();
        void UnInit() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

    private:
        SceneSystem* sceneSystem_ = nullptr;
        std::atomic_bool initialized_{false};
    };
} // namespace ve::editor
