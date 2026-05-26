#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Threading/Thread.h"

#include <memory>
#include <string>

namespace ve
{
    struct GameThreadSystemImpl;
    class ResourceManager;
    class RenderSystem;
    class Scene;

    /// Current phase of the Game Thread frame tick.
    enum class GameThreadPhase
    {
        Uninitialized,
        Idle,
        BeginFrame,
        Lifecycle,
        Update,
        LateUpdate,
        TransformUpdate,
        RenderExtraction,
        EndFrame,
        ShuttingDown,
    };

    /// Describes the dedicated Game Thread created by GameThreadSystem::Initialize().
    struct GameThreadSystemDesc
    {
        /// Diagnostic name copied into the owned Game Thread.
        std::string threadName = "VEngineGameThread";
    };

    /// Owns the Game Thread execution context, frame counter, tick phases, and scene mutation rule.
    ///
    /// GameThreadSystem owns one dedicated Game Thread. The Game Thread advances frames from its own loop after
    /// initialization. Scene mutation APIs should use CheckGameThreadAccess() or ValidateGameThreadAccess() to enforce
    /// that live scene state is changed only on the Game Thread.
    class GameThreadSystem : public NonMovable
    {
    public:
        GameThreadSystem();
        ~GameThreadSystem();

        /// Starts the dedicated Game Thread and begins accepting frame tick requests.
        ///
        /// Returns InvalidState when called while already initialized. A standalone GameThreadSystem object may be
        /// initialized again after Shutdown() completes.
        [[nodiscard]] ErrorCode Initialize(const GameThreadSystemDesc& desc);

        /// Stops accepting frame requests, wakes the Game Thread, and joins it.
        ///
        /// Shutdown is a no-op when the system is not initialized.
        void Shutdown() noexcept;

        /// Returns true after Initialize() succeeds and before Shutdown() completes.
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Returns true when the calling thread is the owned Game Thread.
        [[nodiscard]] bool CheckGameThreadAccess() const noexcept;

        /// Asserts in debug builds when the calling thread is not the owned Game Thread.
        void ValidateGameThreadAccess() const noexcept;

        /// Returns the last known id of the owned Game Thread.
        [[nodiscard]] ThreadId GetGameThreadId() const noexcept;

        /// Returns the current Game Thread phase.
        [[nodiscard]] GameThreadPhase GetCurrentPhase() const noexcept;

        /// Returns the one-based frame id currently executing, or the last executed frame id while idle.
        [[nodiscard]] UInt64 GetFrameIndex() const noexcept;

        /// Returns the number of successfully completed Game Thread frames.
        [[nodiscard]] UInt64 GetCompletedFrameCount() const noexcept;

        /// Connects the RenderSystem frame boundary driven by the Game Thread.
        ///
        /// During RenderExtraction, the Game Thread submits a scene snapshot when an active scene is bound. During
        /// EndFrame, GameThreadSystem inserts a Render Thread fence and waits with a one-frame-lag policy so the Game
        /// Thread cannot run unbounded ahead of the Render Thread.
        [[nodiscard]] ErrorCode SetRenderSystem(RenderSystem* renderSystem) noexcept;

        /// Disconnects the RenderSystem frame boundary and waits until any in-progress render extraction has returned.
        void ClearRenderSystem() noexcept;

        /// Sets the active scene updated and extracted by the Game Thread.
        ///
        /// The caller owns the Scene lifetime and must keep it alive until ClearActiveScene() or Shutdown() completes.
        [[nodiscard]] ErrorCode SetActiveScene(Scene* scene, ResourceManager* resourceManager) noexcept;

        /// Clears the active scene and waits until any in-progress scene frame has returned.
        void ClearActiveScene() noexcept;

    private:
        std::unique_ptr<GameThreadSystemImpl> impl_;
    };
} // namespace ve
