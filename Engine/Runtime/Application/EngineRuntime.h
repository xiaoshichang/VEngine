#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/IO/IOSystem.h"
#include "Engine/Runtime/Jobs/JobSystem.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Scene/SceneSystem.h"

namespace ve
{
    enum class EngineRuntimeState
    {
        NotInitialized,
        Initialized,
        Shutdown,
    };

    /// Describes long-lived runtime services owned by EngineRuntime.
    ///
    /// Player, Editor, tools, and future platform backends pass this descriptor into EngineRuntime so service
    /// configuration stays explicit instead of relying on globals or per-entry-point initialization code.
    struct EngineRuntimeInitParam
    {
        /// Configuration for the worker-thread Job System service.
        JobSystemInitParam jobSystem;

        /// Configuration for the dedicated file IO system service.
        IOSystemInitParam ioSystem;

        /// Configuration for the Scene Thread and active Scene service.
        SceneSystemInitParam sceneSystem;

        /// Configuration for the Render Thread and render command queue service.
        RenderSystemInitParam renderSystem;
    };

    /// Owns the shared runtime service lifecycle for Player, Editor, and tools.
    ///
    /// EngineRuntime sits below Application's platform loop and above individual runtime modules. It initializes and
    /// shuts down long-lived services in a deterministic order, and exposes references to those services without using
    /// a global singleton. The first version owns JobSystem, IOSystem, SceneSystem, and RenderSystem; Resource, Input,
    /// Script, UI, and Physics can connect through this layer as those modules land.
    class EngineRuntime : public NonMovable
    {
    public:
        EngineRuntime();
        ~EngineRuntime();

        /// Initializes runtime services.
        ///
        /// Returns InvalidState when this runtime is already initialized or when it has completed a previous successful
        /// lifecycle. A single EngineRuntime object is intentionally one-shot so Player and Editor startup/shutdown
        /// order stays simple and visible. Service initialization failures are treated as unrecoverable startup
        /// failures: they are logged as fatal errors and terminate the process rather than returning partial runtime
        /// state to the caller.
        [[nodiscard]] ErrorCode Initialize(const EngineRuntimeInitParam& desc);

        /// Shuts down initialized services in reverse ownership order.
        ///
        /// Calling Shutdown() before Initialize() is a no-op. Shutdown() may be called repeatedly after the first
        /// shutdown, but Initialize() cannot be called again on the same EngineRuntime object afterward.
        void Shutdown() noexcept;

        /// Returns true after Initialize() succeeds and before Shutdown() completes.
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Returns true once this runtime has completed one successful Initialize() call.
        [[nodiscard]] bool HasInitialized() const noexcept;

        /// Returns the runtime-owned Job System.
        ///
        /// The runtime must be initialized before callers use the returned service.
        [[nodiscard]] JobSystem& GetJobSystem() noexcept;

        /// Returns the runtime-owned Job System.
        ///
        /// The runtime must be initialized before callers use the returned service.
        [[nodiscard]] const JobSystem& GetJobSystem() const noexcept;

        /// Returns the runtime-owned IO System.
        ///
        /// The runtime must be initialized before callers use the returned service.
        [[nodiscard]] IOSystem& GetIOSystem() noexcept;

        /// Returns the runtime-owned IO System.
        ///
        /// The runtime must be initialized before callers use the returned service.
        [[nodiscard]] const IOSystem& GetIOSystem() const noexcept;

        /// Returns the runtime-owned Render System.
        ///
        /// The runtime must be initialized before callers use the returned service.
        [[nodiscard]] RenderSystem& GetRenderSystem() noexcept;

        /// Returns the runtime-owned Render System.
        ///
        /// The runtime must be initialized before callers use the returned service.
        [[nodiscard]] const RenderSystem& GetRenderSystem() const noexcept;

        /// Returns the runtime-owned Scene System.
        ///
        /// The runtime must be initialized before callers use the returned service.
        [[nodiscard]] SceneSystem& GetSceneSystem() noexcept;

        /// Returns the runtime-owned Scene System.
        ///
        /// The runtime must be initialized before callers use the returned service.
        [[nodiscard]] const SceneSystem& GetSceneSystem() const noexcept;

    private:
        JobSystem jobSystem_;
        IOSystem ioSystem_;
        SceneSystem sceneSystem_;
        RenderSystem renderSystem_;
        EngineRuntimeState state_ = EngineRuntimeState::NotInitialized;
    };
} // namespace ve
