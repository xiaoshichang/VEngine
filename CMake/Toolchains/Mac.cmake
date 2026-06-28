set(VE_TOOLCHAIN_PLATFORM "macOS" CACHE STRING "VEngine target platform")
set(VE_MAC_PLATFORM "MACOS" CACHE STRING "macOS target platform")
set(VE_MAC_ARCHITECTURES "arm64" CACHE STRING "macOS target architectures")

set_property(CACHE VE_MAC_PLATFORM PROPERTY STRINGS MACOS)

set(CMAKE_SYSTEM_NAME Darwin CACHE STRING "Target system name" FORCE)
set(CMAKE_OSX_ARCHITECTURES "${VE_MAC_ARCHITECTURES}" CACHE STRING "macOS architectures" FORCE)

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS deployment target")
endif()

set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "YES" CACHE STRING "Build only the active architecture on macOS" FORCE)

if(NOT CMAKE_HOST_APPLE)
    message(STATUS "macOS toolchain selected on a non-Apple host; configure with Xcode on macOS to build macOS targets.")
endif()
