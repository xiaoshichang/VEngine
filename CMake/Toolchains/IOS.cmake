set(VE_TOOLCHAIN_PLATFORM "iOS" CACHE STRING "VEngine target platform")
set(VE_APPLE_PLATFORM "IOS" CACHE STRING "Apple target platform")
set(VE_IOS_SDK "iphoneos" CACHE STRING "iOS SDK")
set(VE_IOS_ARCHITECTURES "arm64" CACHE STRING "iOS target architectures")
set(VE_IOS_DEPLOYMENT_TARGET "17.0" CACHE STRING "Minimum supported iOS version")

set_property(CACHE VE_APPLE_PLATFORM PROPERTY STRINGS IOS)
set_property(CACHE VE_IOS_SDK PROPERTY STRINGS iphoneos iphonesimulator)

set(CMAKE_SYSTEM_NAME iOS CACHE STRING "Target system name" FORCE)
set(CMAKE_OSX_SYSROOT "${VE_IOS_SDK}" CACHE STRING "iOS SDK root" FORCE)
set(CMAKE_OSX_ARCHITECTURES "${VE_IOS_ARCHITECTURES}" CACHE STRING "iOS architectures" FORCE)

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${VE_IOS_DEPLOYMENT_TARGET}" CACHE STRING "Minimum iOS deployment target")
endif()

set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "YES" CACHE STRING "Build only the active architecture for iOS debug builds" FORCE)
set(CMAKE_XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator" CACHE STRING "Supported iOS platforms" FORCE)

if(NOT CMAKE_HOST_APPLE)
    message(STATUS "iOS toolchain selected on a non-Apple host; configure and build iOS targets with Xcode on macOS.")
endif()
