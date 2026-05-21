include_guard(GLOBAL)

function(ve_add_rhi_demos)
    if(NOT VE_BUILD_RHI_DEMOS)
        return()
    endif()

    if(WIN32)
        if(VE_ENABLE_D3D11)
            add_executable(VEngineRhiD3D11TriangleDemo
                Tests/Demos/RHI/Windows/D3D11TriangleDemo.cpp
                Tests/Demos/RHI/Windows/WindowsTriangleDemo.cpp
                Tests/Demos/RHI/Windows/WindowsTriangleDemo.h
            )

            target_link_libraries(VEngineRhiD3D11TriangleDemo
                PRIVATE
                    VEngine
                    user32
            )

            ve_configure_target(VEngineRhiD3D11TriangleDemo)
        endif()

        if(VE_ENABLE_D3D12)
            add_executable(VEngineRhiD3D12TriangleDemo
                Tests/Demos/RHI/Windows/D3D12TriangleDemo.cpp
                Tests/Demos/RHI/Windows/WindowsTriangleDemo.cpp
                Tests/Demos/RHI/Windows/WindowsTriangleDemo.h
            )

            target_link_libraries(VEngineRhiD3D12TriangleDemo
                PRIVATE
                    VEngine
                    user32
            )

            ve_configure_target(VEngineRhiD3D12TriangleDemo)
        endif()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND VE_ENABLE_METAL)
        enable_language(OBJCXX)

        find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
        find_library(UIKIT_FRAMEWORK UIKit REQUIRED)

        add_executable(VEngineRhiMetalTriangleDemo MACOSX_BUNDLE
            Tests/Demos/RHI/iOS/MetalTriangleDemo.mm
        )

        target_link_libraries(VEngineRhiMetalTriangleDemo
            PRIVATE
                VEngine
                ${FOUNDATION_FRAMEWORK}
                ${UIKIT_FRAMEWORK}
                "-framework QuartzCore"
                "-framework Metal"
        )

        set_target_properties(VEngineRhiMetalTriangleDemo
            PROPERTIES
                MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/Tests/Demos/RHI/iOS/Info.plist.in
                XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.vengine.tests.rhi.metaltriangle"
                XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
                XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphonesimulator iphoneos"
        )

        ve_configure_target(VEngineRhiMetalTriangleDemo)
    endif()
endfunction()
