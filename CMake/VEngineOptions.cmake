include_guard(GLOBAL)

option(VE_BUILD_PLAYER "Build player application" ON)
option(VE_BUILD_EDITOR "Build editor application" ON)
option(VE_BUILD_TESTS "Build tests" ON)
option(VE_BUILD_TOOLS "Build command line tools" ON)
option(VE_BUILD_MAC_PLAYER "Build macOS player app" OFF)

option(VE_ENABLE_D3D11 "Enable D3D11 RHI" ON)
option(VE_ENABLE_D3D12 "Enable D3D12 RHI" ON)
option(VE_ENABLE_METAL "Enable Metal RHI" OFF)

set(VE_MAC_BUNDLE_IDENTIFIER "com.vengine.player" CACHE STRING "macOS player bundle identifier")
