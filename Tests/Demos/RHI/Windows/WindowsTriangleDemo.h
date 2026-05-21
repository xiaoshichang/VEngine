#pragma once

#include "Engine/RHI/Common/RhiDevice.h"

#include <cstdint>
#include <memory>

namespace ve::tests
{
/// Runs a Win32-hosted RHI triangle demo with the provided backend device.
int RunWindowsTriangleDemo(const char* title, std::unique_ptr<rhi::RhiDevice> device, uint32_t maxFrames = 0);
}
