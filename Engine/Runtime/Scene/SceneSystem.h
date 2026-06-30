#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Input/InputSystem.h"
#include "Engine/Runtime/Render/RenderFramePipeline.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Resource/AssetRecord.h"
#include "Engine/Runtime/Resource/ResourceSystem.h"
#include "Engine/Runtime/Scene/OSEventQueue.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Threading/FrameEndSync.h"
#include "Engine/Runtime/Threading/Thread.h"
#include "Engine/Runtime/Time/Time.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ve
{
    struct SceneSystemImpl;
    class ScriptingSystem;

    /// SceneThread callbacks supplied by Editor for per-frame event and render hooks.
    struct SceneSystemEditorCallback
    {
        std::function<void()> onBeforeOSEvents = nullptr;
        std::function<void()> onStartFrame = nullptr;
        /// Handles an OS event before runtime input processing.
        ///
        /// Return true when the event should continue to InputSystem. This lets Editor-owned views decide when
        /// keyboard and mouse input belongs to game runtime input instead of editor UI.
        std::function<bool(const OSEvent& event)> onOSEvent = nullptr;
        std::function<std::shared_ptr<FrameRenderPipeline>()> onRender = nullptr;
    };

    /// Describes the Scene Thread created by SceneSystem::Initialize().
    struct SceneSystemInitParam
    {
        /// Diagnostic name copied into the owned Scene Thread.
        std::string threadName = "VEngineSceneThread";
    };

    enum class SceneLoadMode
    {
        Single,
        Additive,
    };

    enum class SceneLoadSource
    {
        Asset,
        Text,
    };

    struct SceneLoadRequest
    {
        SceneLoadSource source = SceneLoadSource::Asset;
        AssetID scene;
        std::string_view sceneText;
        SceneLoadMode mode = SceneLoadMode::Single;
        SceneExecutionMode executionMode = SceneExecutionMode::Runtime;
        const IAssetRecordProvider* provider = nullptr;
        ResourceSystem* resourceSystem = nullptr;
        ScriptingSystem* scriptingSystem = nullptr;
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
        [[nodiscard]] ErrorCode Initialize(const SceneSystemInitParam& initParam, TimeSystem& timeSystem, InputSystem& inputSystem, RenderSystem& renderSystem);

        /// Stops Scene updates and joins the Scene Thread.
        ///
        /// Shutdown is a no-op when the system is not initialized. The active Scene is preserved until the SceneSystem
        /// is destroyed or LoadScene() is called, so callers can inspect or explicitly clear it after stopping
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

        [[nodiscard]] Error LoadScene(const SceneLoadRequest& request);
        void UnloadActiveScene() noexcept;

        /// Queues one OS event for Scene Thread processing.
        void EnqueueOSEvent(const OSEvent& event);

        /// Marks one Main Thread frame end and blocks when Main Thread is ahead by more than one frame.
        void NotifyMainThreadFrameEnd();

        /// Assigns the Main-Scene frame-end sync primitive used by this SceneSystem.
        void SetMainThreadSceneThreadFrameEndSync(MainThreadSceneThreadFrameEndSync* sync) noexcept;

        /// Assigns the Scene-Render frame-end sync primitive used by this SceneSystem.
        void SetSceneThreadRenderThreadFrameEndSync(SceneThreadRenderThreadFrameEndSync* sync) noexcept;

        /// Assigns callbacks executed on the Scene Thread for Editor integration.
        void SetEditorCallback(SceneSystemEditorCallback callback) noexcept;

        /// Assigns the runtime hook executed on the Scene Thread after OS events and before Scene update.
        ///
        /// Player and future runtime shells use this for frame-start work that must happen on the Scene Thread without
        /// depending on Editor-only callbacks.
        void SetRuntimeStartFrameCallback(std::function<void()> callback) noexcept;

        /// Assigns the runtime OS-event hook executed on the Scene Thread before editor/input dispatch.
        ///
        /// Player and future runtime views use this to keep render-facing scene state, such as ViewportClient size,
        /// on the Scene Thread. The callback is intentionally separate from editor callbacks so tools cannot
        /// accidentally replace player/runtime viewport maintenance.
        void SetRuntimeOSEventCallback(std::function<void(const OSEvent& event)> callback) noexcept;

        /// notify scene thread to start main loop.
        void StartLoop() noexcept;

    private:
        friend class Scene;

        Result<std::unique_ptr<Scene>> BuildSceneFromRequest(const SceneLoadRequest& request);
        void StopAndJoinSceneThread() noexcept;
        void EnqueueRenderCommand(RenderCommand command);

        std::unique_ptr<SceneSystemImpl> impl_;
    };
} // namespace ve
