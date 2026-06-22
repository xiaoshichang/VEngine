include_guard(GLOBAL)

option(VE_BUILD_PLAYER "Build Windows player" ON)
option(VE_BUILD_EDITOR "Build Windows editor" ON)
option(VE_BUILD_TESTS "Build Windows tests" ON)
option(VE_BUILD_TOOLS "Build command line tools" ON)
option(VE_BUILD_IOS_PLAYER "Build iOS player app" OFF)

option(VE_ENABLE_D3D11 "Enable D3D11 RHI" ON)
option(VE_ENABLE_D3D12 "Enable D3D12 RHI" ON)
option(VE_ENABLE_METAL "Enable Metal RHI" OFF)

set(VE_IOS_BUNDLE_IDENTIFIER "com.vengine.player" CACHE STRING "iOS player bundle identifier")
