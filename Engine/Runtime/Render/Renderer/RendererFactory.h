#pragma once

#include "Engine/Runtime/Render/Renderer/BaseRenderer.h"

#include <memory>

namespace ve
{
    /// Selects the player renderer from the compile-time product platform.
    [[nodiscard]] std::unique_ptr<BaseRenderer> CreatePlayerRenderer(BaseRendererInitParam initParam);
} // namespace ve
