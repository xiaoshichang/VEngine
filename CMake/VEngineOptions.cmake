include_guard(GLOBAL)

option(VE_BUILD_PLAYER "Build player application" ON)
option(VE_BUILD_EDITOR "Build editor application" ON)
option(VE_BUILD_TESTS "Build tests" ON)
option(VE_BUILD_TOOLS "Build command line tools" ON)
option(VE_BUILD_MAC_PLAYER "Build macOS player app" OFF)
option(VE_BUILD_IOS_PLAYER "Build iOS player app" OFF)

option(VE_ENABLE_D3D11 "Enable D3D11 RHI" ON)
option(VE_ENABLE_D3D12 "Enable D3D12 RHI" ON)
option(VE_ENABLE_METAL "Enable Metal RHI" OFF)

set(VE_MAC_BUNDLE_IDENTIFIER "com.vengine.player" CACHE STRING "macOS player bundle identifier")
set(VE_IOS_BUNDLE_IDENTIFIER "com.vengine.iosplayer" CACHE STRING "iOS player bundle identifier")
set(VE_IOS_DEVELOPMENT_TEAM "" CACHE STRING "Apple development team identifier used for iOS code signing")
set(VE_IOS_CODE_SIGN_STYLE "Automatic" CACHE STRING "iOS Xcode code signing style")
set(VE_IOS_PROVISIONING_PROFILE_SPECIFIER "" CACHE STRING "Optional iOS provisioning profile specifier for manual signing")
set(VE_IOS_CODE_SIGN_IDENTITY "" CACHE STRING "Optional iOS code signing identity, such as Apple Development or Apple Distribution")
set(VE_IOS_DEPLOYMENT_TARGET "17.0" CACHE STRING "Minimum supported iOS version")
set(VE_IOS_PACKAGE_DATA_ROOT "" CACHE PATH "Optional packaged Data directory copied into the iOS app bundle")
set(VE_IOS_NATIVEAOT_LIBRARY "" CACHE FILEPATH "Optional .NET NativeAOT static library linked into the iOS player")
set(VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR "" CACHE PATH "Optional .NET NativeAOT iOS runtime native library directory")
set_property(CACHE VE_IOS_CODE_SIGN_STYLE PROPERTY STRINGS Automatic Manual)

function(ve_is_valid_ios_bundle_identifier value outVariable)
    if(value MATCHES "^[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?(\\.[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?)*$")
        set(${outVariable} TRUE PARENT_SCOPE)
    else()
        set(${outVariable} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(ve_is_valid_ios_deployment_target value outVariable)
    if(value MATCHES "^[0-9]+(\\.[0-9]+)+$")
        set(${outVariable} TRUE PARENT_SCOPE)
    else()
        set(${outVariable} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(ve_validate_ios_options)
    if(NOT VE_BUILD_IOS_PLAYER AND NOT (CMAKE_SYSTEM_NAME STREQUAL "iOS"))
        return()
    endif()

    if(VE_BUILD_IOS_PLAYER AND NOT (CMAKE_SYSTEM_NAME STREQUAL "iOS"))
        message(FATAL_ERROR "VE_BUILD_IOS_PLAYER requires configuring with CMake/Toolchains/IOS.cmake.")
    endif()

    if(DEFINED VE_IOS_SDK AND NOT VE_IOS_SDK MATCHES "^(iphoneos|iphonesimulator)$")
        message(FATAL_ERROR "Invalid VE_IOS_SDK: ${VE_IOS_SDK}. Use iphoneos or iphonesimulator.")
    endif()

    ve_is_valid_ios_bundle_identifier("${VE_IOS_BUNDLE_IDENTIFIER}" ve_valid_ios_bundle_identifier)
    if(NOT ve_valid_ios_bundle_identifier)
        message(FATAL_ERROR
            "Invalid VE_IOS_BUNDLE_IDENTIFIER: ${VE_IOS_BUNDLE_IDENTIFIER}. "
            "Use reverse-DNS segments containing only letters, numbers, and hyphens."
        )
    endif()

    if(NOT VE_IOS_CODE_SIGN_STYLE STREQUAL "Automatic" AND NOT VE_IOS_CODE_SIGN_STYLE STREQUAL "Manual")
        message(FATAL_ERROR "Invalid VE_IOS_CODE_SIGN_STYLE: ${VE_IOS_CODE_SIGN_STYLE}. Use Automatic or Manual.")
    endif()

    if(VE_IOS_CODE_SIGN_STYLE STREQUAL "Manual" AND NOT VE_IOS_PROVISIONING_PROFILE_SPECIFIER)
        message(FATAL_ERROR "Manual iOS code signing requires VE_IOS_PROVISIONING_PROFILE_SPECIFIER.")
    endif()

    ve_is_valid_ios_deployment_target("${VE_IOS_DEPLOYMENT_TARGET}" ve_valid_ios_deployment_target)
    if(NOT ve_valid_ios_deployment_target)
        message(FATAL_ERROR "Invalid VE_IOS_DEPLOYMENT_TARGET: ${VE_IOS_DEPLOYMENT_TARGET}. Use a numeric version such as 17.0.")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT (CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL VE_IOS_DEPLOYMENT_TARGET))
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${VE_IOS_DEPLOYMENT_TARGET}" CACHE STRING "Minimum iOS deployment target" FORCE)
    endif()

    if(VE_IOS_PACKAGE_DATA_ROOT AND NOT EXISTS "${VE_IOS_PACKAGE_DATA_ROOT}")
        message(FATAL_ERROR "VE_IOS_PACKAGE_DATA_ROOT does not exist: ${VE_IOS_PACKAGE_DATA_ROOT}")
    endif()

    if(VE_IOS_NATIVEAOT_LIBRARY)
        if(NOT EXISTS "${VE_IOS_NATIVEAOT_LIBRARY}")
            message(FATAL_ERROR "VE_IOS_NATIVEAOT_LIBRARY does not exist: ${VE_IOS_NATIVEAOT_LIBRARY}")
        endif()

        if(NOT VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR)
            message(FATAL_ERROR "VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR must be set when VE_IOS_NATIVEAOT_LIBRARY is set.")
        endif()

        if(NOT EXISTS "${VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR}")
            message(FATAL_ERROR "VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR does not exist: ${VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR}")
        endif()
    elseif(VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR)
        message(WARNING "VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR is set but VE_IOS_NATIVEAOT_LIBRARY is empty; the NativeAOT runtime inputs will not be linked.")
    endif()
endfunction()

ve_validate_ios_options()
