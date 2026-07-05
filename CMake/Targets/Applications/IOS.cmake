include_guard(GLOBAL)

function(ve_add_ios_player)
    if(NOT VE_BUILD_IOS_PLAYER)
        return()
    endif()

    enable_language(OBJCXX)

    add_executable(VEngineIOSPlayer MACOSX_BUNDLE
        Player/Windows/VEnginePlayer.cpp
        Player/Windows/VEnginePlayer.h
        Player/iOS/IOSPlayer.mm
        Player/iOS/LaunchScreen.storyboard
    )

    target_link_libraries(VEngineIOSPlayer
        PRIVATE
            VEngine
            "-framework Foundation"
            "-framework UIKit"
            "-framework Metal"
            "-framework QuartzCore"
    )

    if(VE_IOS_NATIVEAOT_LIBRARY)
        if(NOT EXISTS "${VE_IOS_NATIVEAOT_LIBRARY}")
            message(FATAL_ERROR "VE_IOS_NATIVEAOT_LIBRARY does not exist: ${VE_IOS_NATIVEAOT_LIBRARY}")
        endif()

        target_link_libraries(VEngineIOSPlayer
            PRIVATE
                "${VE_IOS_NATIVEAOT_LIBRARY}"
        )

        target_link_options(VEngineIOSPlayer
            PRIVATE
                "-Wl,-force_load,${VE_IOS_NATIVEAOT_LIBRARY}"
        )

        if(NOT VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR)
            message(FATAL_ERROR "VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR must point at the iOS NativeAOT runtime native directory when VE_IOS_NATIVEAOT_LIBRARY is set.")
        endif()

        if(NOT EXISTS "${VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR}")
            message(FATAL_ERROR "VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR does not exist: ${VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR}")
        endif()

        set(ve_ios_nativeaot_runtime_inputs
            libSystem.Native.a
            libSystem.IO.Compression.Native.a
            libSystem.Net.Security.Native.a
            libSystem.Security.Cryptography.Native.Apple.a
            libbootstrapperdll.o
            libRuntime.WorkstationGC.a
            libeventpipe-disabled.a
            libstandalonegc-disabled.a
            libaotminipal.a
            libstdc++compat.a
            libbrotlienc.a
            libbrotlidec.a
            libbrotlicommon.a
        )

        foreach(ve_ios_nativeaot_runtime_input IN LISTS ve_ios_nativeaot_runtime_inputs)
            set(ve_ios_nativeaot_runtime_path "${VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR}/${ve_ios_nativeaot_runtime_input}")
            if(NOT EXISTS "${ve_ios_nativeaot_runtime_path}")
                message(FATAL_ERROR "Required iOS NativeAOT runtime input does not exist: ${ve_ios_nativeaot_runtime_path}")
            endif()

            target_link_libraries(VEngineIOSPlayer
                PRIVATE
                    "${ve_ios_nativeaot_runtime_path}"
            )
        endforeach()

        target_link_libraries(VEngineIOSPlayer
            PRIVATE
                "-framework CoreFoundation"
                "-framework CryptoKit"
                "-framework Network"
                "-framework Security"
                "-framework GSS"
                "-L/usr/lib/swift"
                "-lswiftCore"
                "-lswiftFoundation"
                dl
                objc
                z
                icucore
                m
        )
    endif()

    set_source_files_properties(Player/iOS/LaunchScreen.storyboard
        PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources
    )
    set_source_files_properties(Player/iOS/IOSPlayer.mm
        PROPERTIES
            COMPILE_OPTIONS "-fobjc-arc"
    )

    if(VE_IOS_ORIENTATION STREQUAL "Portrait")
        set(VE_IOS_SUPPORTED_INTERFACE_ORIENTATIONS_PLIST
"<array>
        <string>UIInterfaceOrientationPortrait</string>
    </array>")
    else()
        set(VE_IOS_SUPPORTED_INTERFACE_ORIENTATIONS_PLIST
"<array>
        <string>UIInterfaceOrientationLandscapeLeft</string>
        <string>UIInterfaceOrientationLandscapeRight</string>
    </array>")
    endif()
    set(VE_IOS_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Generated/Player/iOS/Info.plist")
    configure_file(
        ${PROJECT_SOURCE_DIR}/Player/iOS/Info.plist.in
        ${VE_IOS_INFO_PLIST}
        @ONLY
    )

    set_target_properties(VEngineIOSPlayer
        PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${VE_IOS_INFO_PLIST}
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${VE_IOS_BUNDLE_IDENTIFIER}"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${VE_IOS_DEVELOPMENT_TEAM}"
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "${VE_IOS_CODE_SIGN_STYLE}"
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${VE_IOS_DEPLOYMENT_TARGET}"
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator"
            XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    )

    if(VE_IOS_PROVISIONING_PROFILE_SPECIFIER)
        set_target_properties(VEngineIOSPlayer
            PROPERTIES
                XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "${VE_IOS_PROVISIONING_PROFILE_SPECIFIER}"
        )
    endif()

    if(VE_IOS_CODE_SIGN_IDENTITY)
        set_target_properties(VEngineIOSPlayer
            PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${VE_IOS_CODE_SIGN_IDENTITY}"
        )
    endif()

    if(VE_IOS_PACKAGE_DATA_ROOT)
        add_custom_command(TARGET VEngineIOSPlayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_BUNDLE_DIR:VEngineIOSPlayer>/Data"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${VE_IOS_PACKAGE_DATA_ROOT}" "$<TARGET_BUNDLE_DIR:VEngineIOSPlayer>/Data"
            COMMENT "Copying packaged iOS runtime data"
        )
    endif()

    ve_configure_target(VEngineIOSPlayer)
endfunction()
