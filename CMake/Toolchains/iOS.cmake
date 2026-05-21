set(VE_TOOLCHAIN_PLATFORM "iOS" CACHE STRING "VEngine target platform")
set(VE_IOS_PLATFORM "SIMULATOR" CACHE STRING "iOS target platform: SIMULATOR or DEVICE")
set(VE_IOS_ARCHITECTURES "arm64" CACHE STRING "iOS target architectures")

set_property(CACHE VE_IOS_PLATFORM PROPERTY STRINGS SIMULATOR DEVICE)

set(CMAKE_SYSTEM_NAME iOS CACHE STRING "Target system name" FORCE)

if(VE_IOS_PLATFORM STREQUAL "SIMULATOR")
    set(CMAKE_OSX_SYSROOT "iphonesimulator" CACHE STRING "iOS SDK" FORCE)
elseif(VE_IOS_PLATFORM STREQUAL "DEVICE")
    set(CMAKE_OSX_SYSROOT "iphoneos" CACHE STRING "iOS SDK" FORCE)
else()
    message(FATAL_ERROR "VE_IOS_PLATFORM must be SIMULATOR or DEVICE.")
endif()

set(CMAKE_OSX_ARCHITECTURES "${VE_IOS_ARCHITECTURES}" CACHE STRING "iOS architectures" FORCE)

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS deployment target")
endif()

if(NOT CMAKE_HOST_APPLE)
    message(STATUS "iOS toolchain selected on a non-Apple host; configure with Xcode on macOS to build iOS targets.")
endif()
