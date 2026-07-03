#include "Engine/Runtime/Core/Version.h"
#include "Engine/Runtime/Logging/Log.h"

#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

@interface VEngineIOSMetalView : UIView
@end

@implementation VEngineIOSMetalView

+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil)
    {
        self.backgroundColor = [UIColor colorWithRed:0.031f green:0.043f blue:0.059f alpha:1.0f];

        CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
        metalLayer.opaque = YES;
        metalLayer.contentsScale = UIScreen.mainScreen.scale;
        metalLayer.drawableSize = CGSizeMake(frame.size.width * metalLayer.contentsScale, frame.size.height * metalLayer.contentsScale);
    }
    return self;
}

- (void)layoutSubviews
{
    [super layoutSubviews];

    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
    UIScreen* screen = self.window != nil ? self.window.screen : UIScreen.mainScreen;
    metalLayer.contentsScale = screen.scale;
    metalLayer.drawableSize = CGSizeMake(self.bounds.size.width * metalLayer.contentsScale, self.bounds.size.height * metalLayer.contentsScale);
}

@end

@interface VEngineIOSViewController : UIViewController
@end

@implementation VEngineIOSViewController

- (void)loadView
{
    self.view = [[VEngineIOSMetalView alloc] initWithFrame:UIScreen.mainScreen.bounds];
}

- (BOOL)prefersHomeIndicatorAutoHidden
{
    return YES;
}

@end

@interface VEngineIOSAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation VEngineIOSAppDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    (void)application;
    (void)launchOptions;

    ve::LoggingConfig loggingConfig = ve::MakeDefaultLoggingConfig();
    loggingConfig.enableFile = false;
    loggingConfig.enableConsole = true;
    loggingConfig.enableDebuggerOutput = false;
    static_cast<void>(ve::InitializeLogging(loggingConfig));

    const ve::BuildInfo buildInfo = ve::GetBuildInfo();
    VE_LOG_INFO_CATEGORY("Player", "{} {} starting on {}.", buildInfo.projectName, buildInfo.version, buildInfo.platform);

    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[VEngineIOSViewController alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}

- (void)applicationWillTerminate:(UIApplication*)application
{
    (void)application;
    VE_LOG_INFO_CATEGORY("Player", "VEngine iOS player terminating.");
    ve::ShutdownLogging();
}

@end

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([VEngineIOSAppDelegate class]));
    }
}
