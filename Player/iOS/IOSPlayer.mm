#include "Engine/Runtime/Core/Application.h"
#include "Engine/Runtime/Core/Version.h"

#import <UIKit/UIKit.h>

@interface VEngineAppDelegate : UIResponder <UIApplicationDelegate>

@property(strong, nonatomic) UIWindow* window;

@end

@implementation VEngineAppDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    (void)application;
    (void)launchOptions;

    ve::Application engineApplication("VEngineIOSPlayer");
    engineApplication.RunOnce();

    const ve::BuildInfo buildInfo = ve::GetBuildInfo();
    (void)buildInfo;

    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];

    UIViewController* viewController = [[UIViewController alloc] init];
    viewController.view.backgroundColor = [UIColor blackColor];

    self.window.rootViewController = viewController;
    [self.window makeKeyAndVisible];

    return YES;
}

@end

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([VEngineAppDelegate class]));
    }
}
