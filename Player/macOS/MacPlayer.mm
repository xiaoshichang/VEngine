#include "Engine/Runtime/Application/Application.h"
#include "Engine/Runtime/Core/Version.h"

#import <AppKit/AppKit.h>

@interface VEngineAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation VEngineAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    (void)notification;

    ve::Application engineApplication("VEngineMacPlayer");
    int engineInitResult = engineApplication.Init();
    if (engineInitResult == 0)
    {
        engineApplication.Run();
    }
    engineApplication.UnInit();

    const ve::BuildInfo buildInfo = ve::GetBuildInfo();
    (void)buildInfo;

    [NSApp terminate:nil];
}

@end

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        (void)argc;
        (void)argv;

        [NSApplication sharedApplication];
        VEngineAppDelegate* delegate = [[VEngineAppDelegate alloc] init];
        NSApp.delegate = delegate;
        [NSApp run];
        return 0;
    }
}
