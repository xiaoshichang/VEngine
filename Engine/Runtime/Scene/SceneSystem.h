#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Scene/OSEventQueue.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Threading/FrameEndSync.h"
#include "Engine/Runtime/Threading/Thread.h"
#include "Engine/Runtime/Time/Time.h"
#include "Engine/Runtime/Render/RenderSystem.h"

#include <functional>
#include <memory>
#include <string>

namespace ve
{
    struct SceneSystemImpl;

    /// SceneThread callbacks supplied by Editor for per-frame event and render hooks.
    struct SceneSystemEditorCallback
    {
        std::function<void()> onStartFrame;
        std::function<void(const OSEvent& event)> onOSEvent;
        std::function<void()> onRender;
    };

    /// Describes the Scene Thread created by SceneSystem::Initialize().
    struct SceneSystemInitParam
    {
        /// Diagnostic name copied into the owned Scene Thread.
        std::string threadName = "VEngineSceneThread";
    };

    /// Owns the active Scene and its update thread.
    ///
    /// SceneSystem is the runtime service boundary for live GameObject state. It owns one active Scene object and
    /// updates it on the Scene Thread. Future render extraction should copy or command scene data across thread
    /// boundaries instead of letting Render Thread code read GameObject state directly.
    class SceneSystem : public NonMovable
    {
    public:
        SceneSystem();
        ~SceneSystem();

        /// Creates an empty active Scene and starts the Scene Thread.
        ///
        /// timeSystem must already be initialized by EngineRuntime before SceneSystem starts.
        [[nodiscard]] ErrorCode
        Initialize(const SceneSystemInitParam& initParam, TimeSystem& timeSystem, RenderSystem& renderSystem);

        /// Stops Scene updates and joins the Scene Thread.
        ///
        /// Shutdown is a no-op when the system is not initialized. The active Scene is preserved until the SceneSystem
        /// is destroyed or ReplaceScene() is called, so callers can inspect or explicitly clear it after stopping
        /// updates.
        void Shutdown() noexcept;

        /// Returns true after Initialize() succeeds and before Shutdown() completes.
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Returns the last known id of the owned Scene Thread.
        [[nodiscard]] ThreadId GetSceneThreadId() const noexcept;

        /// Returns the active Scene. The returned pointer remains owned by SceneSystem.
        [[nodiscard]] Scene* GetScene() noexcept;

        /// Returns the active Scene. The returned pointer remains owned by SceneSystem.
        [[nodiscard]] const Scene* GetScene() const noexcept;

        /// Queues one OS event for Scene Thread processing.
        [[nodiscard]] ErrorCode EnqueueOSEvent(const OSEvent& event);

        /// Marks one Main Thread frame end and blocks when Main Thread is ahead by more than one frame.
        void NotifyMainThreadFrameEnd();

        /// Assigns the Main-Scene frame-end sync primitive used by this SceneSystem.
        void SetMainThreadSceneThreadFrameEndSync(MainThreadSceneThreadFrameEndSync* sync) noexcept;

        /// Assigns the Scene-Render frame-end sync primitive used by this SceneSystem.
        void SetSceneThreadRenderThreadFrameEndSync(SceneThreadRenderThreadFrameEndSync* sync) noexcept;

        /// Assigns callbacks executed on the Scene Thread for Editor integration.
        void SetEditorCallback(SceneSystemEditorCallback callback) noexcept;

        /// notify scene thread to start main loop.
        void StartLoop() noexcept;


    private:
        std::unique_ptr<SceneSystemImpl> impl_;
    };
} // namespace ve
