#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Jobs/JobSystem.h"

namespace ve
{
/// Describes long-lived runtime services owned by EngineRuntime.
///
/// Player, Editor, tools, and future platform backends pass this descriptor into EngineRuntime so service configuration
/// stays explicit instead of relying on globals or per-entry-point initialization code.
struct EngineRuntimeDesc
{
    /// Configuration for the worker-thread Job System service.
    JobSystemDesc jobSystem;
};

/// Owns the shared runtime service lifecycle for Player, Editor, and tools.
///
/// EngineRuntime sits below Application's platform loop and above individual runtime modules. It initializes and shuts
/// down long-lived services in a deterministic order, and exposes references to those services without using a global
/// singleton. The first version owns only JobSystem; IO Thread, Render, Scene, Resource, Input, Script, UI, and Physics
/// can connect through this layer as those modules land.
class EngineRuntime : public NonMovable
{
public:
    EngineRuntime();
    ~EngineRuntime();

    /// Initializes runtime services.
    ///
    /// Returns InvalidState when this runtime is already initialized or when it has completed a previous successful
    /// lifecycle. A single EngineRuntime object is intentionally one-shot so Player and Editor startup/shutdown order
    /// stays simple and visible.
    [[nodiscard]] Result<void> Initialize(const EngineRuntimeDesc& desc);

    /// Shuts down initialized services in reverse ownership order.
    ///
    /// Calling Shutdown() before Initialize() is a no-op. Shutdown() may be called repeatedly after the first shutdown,
    /// but Initialize() cannot be called again on the same EngineRuntime object afterward.
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

private:
    JobSystem jobSystem_;
    bool initialized_ = false;
    bool hasInitialized_ = false;
};
}
