#include "Engine/Runtime/Core/Version.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Platform/iOS/IOSWindow.h"
#include "Player/Windows/VEnginePlayer.h"

#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <memory>

namespace
{
    void VEngineIOSLogCallback(const ve::LogRecord& record)
    {
        NSString* category = [NSString stringWithUTF8String:record.category != nullptr ? record.category : "General"];
        NSString* message = [[NSString alloc] initWithBytes:record.message.data() length:record.message.size() encoding:NSUTF8StringEncoding];
        if (message == nil)
        {
            message = @"";
        }

        NSLog(@"[VEngine][%s][%@] %@", ve::ToString(record.severity), category, message);
    }
} // namespace

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
        metalLayer.frame = self.bounds;
    }
    return self;
}

- (void)layoutSubviews
{
    [super layoutSubviews];

    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
    UIScreen* screen = self.window != nil ? self.window.screen : UIScreen.mainScreen;
    metalLayer.contentsScale = screen.scale;
    metalLayer.frame = self.bounds;
    metalLayer.drawableSize = CGSizeMake(self.bounds.size.width * metalLayer.contentsScale, self.bounds.size.height * metalLayer.contentsScale);
    ve::IOSWindow::NotifyRegisteredMainViewResized();
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
@property(nonatomic, strong) CADisplayLink* displayLink;
@end

@implementation VEngineIOSAppDelegate

{
    std::unique_ptr<ve::VEnginePlayer> engineApplication_;
}

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    (void)application;
    (void)launchOptions;

    ve::LoggingConfig loggingConfig = ve::MakeDefaultLoggingConfig();
    loggingConfig.enableFile = false;
    loggingConfig.enableConsole = true;
    loggingConfig.enableDebuggerOutput = false;
    static_cast<void>(ve::InitializeLogging(loggingConfig));
    ve::SetLogCallback(VEngineIOSLogCallback);

    const ve::BuildInfo buildInfo = ve::GetBuildInfo();
    VE_LOG_INFO_CATEGORY("Player", "{} {} starting on {}.", buildInfo.projectName, buildInfo.version, buildInfo.platform);

    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[VEngineIOSViewController alloc] init];
    [self.window makeKeyAndVisible];

    if (![self startEngine])
    {
        VE_LOG_ERROR_CATEGORY("Player", "Failed to start VEngine iOS runtime.");
    }

    return YES;
}

- (BOOL)startEngine
{
    UIView* rootView = self.window.rootViewController.view;
    if (rootView == nil)
    {
        return NO;
    }

    ve::IOSWindow::RegisterMainView((__bridge void*)rootView);

    ve::ApplicationInitParam initParam;
    initParam.name = "VEngineIOSPlayer";
    initParam.mainWindow.title = "VEngine iOS Player";
    initParam.mainWindow.width = static_cast<ve::UInt32>(std::max<CGFloat>(1.0, rootView.bounds.size.width * rootView.contentScaleFactor));
    initParam.mainWindow.height = static_cast<ve::UInt32>(std::max<CGFloat>(1.0, rootView.bounds.size.height * rootView.contentScaleFactor));
    initParam.mainWindow.visible = true;
    initParam.runtime.jobSystem.workerThreadNamePrefix = "VEngineIOSPlayerJobWorker";
    initParam.runtime.ioSystem.threadName = "VEngineIOSPlayerIOThread";
    initParam.runtime.renderSystem.threadName = "VEngineIOSPlayerRenderThread";
    initParam.runtime.renderSystem.device.backend = ve::RenderBackend::Metal;

    engineApplication_ = std::make_unique<ve::VEnginePlayer>(std::move(initParam));
    const int initResult = engineApplication_->Init();
    if (initResult != 0)
    {
        engineApplication_->UnInit();
        engineApplication_.reset();
        return NO;
    }

    engineApplication_->StartMainLoop();
    self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(tickEngine:)];
    [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    VE_LOG_INFO_CATEGORY("Player", "VEngine iOS runtime initialized.");
    return YES;
}

- (void)tickEngine:(CADisplayLink*)displayLink
{
    (void)displayLink;
    if (engineApplication_ == nullptr)
    {
        return;
    }

    if (!engineApplication_->TickMainLoopFrame())
    {
        [self stopEngine];
    }
}

- (void)stopEngine
{
    [self.displayLink invalidate];
    self.displayLink = nil;

    if (engineApplication_ != nullptr)
    {
        engineApplication_->UnInit();
        engineApplication_.reset();
    }

    ve::IOSWindow::ClearRegisteredMainView();
}

- (void)applicationWillResignActive:(UIApplication*)application
{
    (void)application;
    self.displayLink.paused = YES;
}

- (void)applicationDidBecomeActive:(UIApplication*)application
{
    (void)application;
    self.displayLink.paused = NO;
}

- (void)applicationDidEnterBackground:(UIApplication*)application
{
    (void)application;
    self.displayLink.paused = YES;
}

- (void)applicationWillEnterForeground:(UIApplication*)application
{
    (void)application;
    self.displayLink.paused = NO;
}

- (void)applicationWillTerminate:(UIApplication*)application
{
    (void)application;
    VE_LOG_INFO_CATEGORY("Player", "VEngine iOS player terminating.");
    [self stopEngine];
    ve::SetLogCallback(nullptr);
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
