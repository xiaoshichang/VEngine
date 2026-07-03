#pragma once

#include "Engine/Runtime/Scripting/ScriptingTypes.h"

namespace ve
{
    [[nodiscard]] bool HasCompleteNativeAOTScriptBridge(const ManagedScriptEntryPoints& entryPoints) noexcept;
    [[nodiscard]] bool HasLinkedNativeAOTScriptBridge() noexcept;
    [[nodiscard]] ManagedScriptEntryPoints CreateLinkedNativeAOTScriptEntryPoints() noexcept;
} // namespace ve
