set(VE_TOOLCHAIN_PLATFORM "iOS" CACHE STRING "VEngine target platform")
set(VE_APPLE_PLATFORM "IOS" CACHE STRING "Apple target platform")
set(VE_IOS_SDK "iphoneos" CACHE STRING "iOS SDK")
set(VE_IOS_ARCHITECTURES "arm64" CACHE STRING "iOS target architectures")

function(ve_is_valid_ios_version value outVariable)
    if(value MATCHES "^[0-9]+(\\.[0-9]+)+$")
        set(${outVariable} ON PARENT_SCOPE)
    else()
        set(${outVariable} OFF PARENT_SCOPE)
    endif()
endfunction()

function(ve_detect_latest_ios_sdk_version outVariable)
    set(detectedVersion "")

    execute_process(
        COMMAND xcrun --sdk iphoneos --show-sdk-version
        OUTPUT_VARIABLE xcrunSdkVersion
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    ve_is_valid_ios_version("${xcrunSdkVersion}" isValidXcrunSdkVersion)
    if(isValidXcrunSdkVersion)
        set(detectedVersion "${xcrunSdkVersion}")
    endif()

    if(NOT detectedVersion)
        execute_process(
            COMMAND xcodebuild -showsdks
            OUTPUT_VARIABLE xcodebuildSdks
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        string(REGEX MATCHALL "-sdk iphoneos[0-9]+(\\.[0-9]+)+" iphoneosSdkMatches "${xcodebuildSdks}")
        foreach(iphoneosSdkMatch IN LISTS iphoneosSdkMatches)
            string(REGEX REPLACE ".*iphoneos" "" candidateVersion "${iphoneosSdkMatch}")
            ve_is_valid_ios_version("${candidateVersion}" isValidCandidateVersion)
            if(isValidCandidateVersion AND (NOT detectedVersion OR candidateVersion VERSION_GREATER detectedVersion))
                set(detectedVersion "${candidateVersion}")
            endif()
        endforeach()
    endif()

    if(NOT detectedVersion)
        set(detectedVersion "16.4")
    endif()

    set(${outVariable} "${detectedVersion}" PARENT_SCOPE)
endfunction()

ve_detect_latest_ios_sdk_version(veDetectedIOSDeploymentTarget)
set(VE_IOS_DEPLOYMENT_TARGET "${veDetectedIOSDeploymentTarget}" CACHE STRING "Minimum supported iOS version" FORCE)

set_property(CACHE VE_APPLE_PLATFORM PROPERTY STRINGS IOS)
set_property(CACHE VE_IOS_SDK PROPERTY STRINGS iphoneos iphonesimulator)

set(CMAKE_SYSTEM_NAME iOS CACHE STRING "Target system name" FORCE)
set(CMAKE_OSX_ARCHITECTURES "${VE_IOS_ARCHITECTURES}" CACHE STRING "iOS architectures" FORCE)

function(ve_resolve_ios_sdk_path sdkName outVariable)
    execute_process(
        COMMAND xcrun --sdk "${sdkName}" --show-sdk-path
        OUTPUT_VARIABLE sdkPath
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE sdkResult
        ERROR_VARIABLE sdkError
    )

    if(NOT sdkResult EQUAL 0 OR NOT sdkPath)
        message(FATAL_ERROR
            "Failed to resolve iOS SDK path for ${sdkName}.\n"
            "xcrun output: ${sdkError}"
        )
    endif()

    set(${outVariable} "${sdkPath}" PARENT_SCOPE)
endfunction()

ve_resolve_ios_sdk_path("${VE_IOS_SDK}" veIosSdkPath)
set(CMAKE_OSX_SYSROOT "${veIosSdkPath}" CACHE PATH "iOS SDK root" FORCE)

set(CMAKE_OSX_DEPLOYMENT_TARGET "${VE_IOS_DEPLOYMENT_TARGET}" CACHE STRING "Minimum iOS deployment target" FORCE)

set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "YES" CACHE STRING "Build only the active architecture for iOS debug builds" FORCE)
set(CMAKE_XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator" CACHE STRING "Supported iOS platforms" FORCE)

if(NOT CMAKE_HOST_APPLE)
    message(STATUS "iOS toolchain selected on a non-Apple host; configure and build iOS targets with Xcode on macOS.")
endif()
