#include "Engine/RHI/Metal/MetalRhi.h"

#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>
#include <array>
#include <memory>

namespace
{
    struct TriangleVertex
    {
        float position[3] = {};
    };

    const char* MetalTriangleShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexInput
{
    float3 position [[attribute(0)]];
};

struct VertexOutput
{
    float4 position [[position]];
    float3 color;
};

vertex VertexOutput VSMain(VertexInput input [[stage_in]])
{
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    output.color = float3(input.position.x + 0.5, input.position.y + 0.5, 1.0);
    return output;
}

fragment float4 PSMain(VertexOutput input [[stage_in]])
{
    return float4(input.color, 1.0);
}
)";

    bool RenderTriangle(CAMetalLayer* layer, CGSize size)
    {
        std::unique_ptr<ve::rhi::RhiDevice> device = ve::rhi::CreateMetalDevice(false);

        if (device == nullptr)
        {
            return false;
        }

        ve::rhi::RhiSwapchainDesc swapchainDesc = {};
        swapchainDesc.nativeLayer = layer;
        swapchainDesc.width = static_cast<uint32_t>(size.width);
        swapchainDesc.height = static_cast<uint32_t>(size.height);
        swapchainDesc.colorFormat = ve::rhi::RhiFormat::Bgra8Unorm;
        swapchainDesc.bufferCount = 2;
        swapchainDesc.debugName = "MetalTriangleSwapchain";

        std::unique_ptr<ve::rhi::RhiSwapchain> swapchain = device->CreateSwapchain(swapchainDesc);

        if (swapchain == nullptr)
        {
            return false;
        }

        constexpr std::array<TriangleVertex, 3> vertices = {
            TriangleVertex{{0.0f, 0.5f, 0.0f}},
            TriangleVertex{{0.5f, -0.5f, 0.0f}},
            TriangleVertex{{-0.5f, -0.5f, 0.0f}},
        };

        ve::rhi::RhiBufferDesc vertexBufferDesc = {};
        vertexBufferDesc.size = sizeof(TriangleVertex) * vertices.size();
        vertexBufferDesc.usage = ve::rhi::RhiBufferUsage::Vertex;
        vertexBufferDesc.initialData = vertices.data();
        vertexBufferDesc.debugName = "TriangleVertexBuffer";

        std::unique_ptr<ve::rhi::RhiBuffer> vertexBuffer = device->CreateBuffer(vertexBufferDesc);

        if (vertexBuffer == nullptr)
        {
            return false;
        }

        ve::rhi::RhiShaderModuleDesc vertexShaderDesc = {};
        vertexShaderDesc.stage = ve::rhi::RhiShaderStage::Vertex;
        vertexShaderDesc.source = MetalTriangleShaderSource;
        vertexShaderDesc.entryPoint = "VSMain";
        vertexShaderDesc.debugName = "TriangleVertexShader";

        std::unique_ptr<ve::rhi::RhiShaderModule> vertexShader = device->CreateShaderModule(vertexShaderDesc);

        if (vertexShader == nullptr)
        {
            return false;
        }

        ve::rhi::RhiShaderModuleDesc fragmentShaderDesc = {};
        fragmentShaderDesc.stage = ve::rhi::RhiShaderStage::Fragment;
        fragmentShaderDesc.source = MetalTriangleShaderSource;
        fragmentShaderDesc.entryPoint = "PSMain";
        fragmentShaderDesc.debugName = "TriangleFragmentShader";

        std::unique_ptr<ve::rhi::RhiShaderModule> fragmentShader = device->CreateShaderModule(fragmentShaderDesc);

        if (fragmentShader == nullptr)
        {
            return false;
        }

        ve::rhi::RhiVertexAttributeDesc positionAttribute = {};
        positionAttribute.semanticName = "POSITION";
        positionAttribute.semanticIndex = 0;
        positionAttribute.format = ve::rhi::RhiFormat::Rgb32Float;
        positionAttribute.offset = 0;

        ve::rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
        pipelineDesc.vertexShader = vertexShader.get();
        pipelineDesc.fragmentShader = fragmentShader.get();
        pipelineDesc.vertexLayout.attributes = &positionAttribute;
        pipelineDesc.vertexLayout.attributeCount = 1;
        pipelineDesc.vertexLayout.stride = sizeof(TriangleVertex);
        pipelineDesc.topology = ve::rhi::RhiPrimitiveTopology::TriangleList;
        pipelineDesc.colorFormat = swapchain->GetColorFormat();
        pipelineDesc.debugName = "TrianglePipeline";

        std::unique_ptr<ve::rhi::RhiPipelineState> pipelineState = device->CreateGraphicsPipeline(pipelineDesc);

        if (pipelineState == nullptr)
        {
            return false;
        }

        std::unique_ptr<ve::rhi::RhiCommandList> commandList = device->CreateCommandList();

        if (commandList == nullptr)
        {
            return false;
        }

        ve::rhi::RhiRenderPassDesc renderPassDesc = {};
        renderPassDesc.debugName = "MetalTriangleDemoPass";
        renderPassDesc.colorAttachmentCount = 1;
        renderPassDesc.colorAttachments[0].loadAction = ve::rhi::RhiLoadAction::Clear;
        renderPassDesc.colorAttachments[0].storeAction = ve::rhi::RhiStoreAction::Store;
        renderPassDesc.colorAttachments[0].clearColor = {0.05f, 0.07f, 0.10f, 1.0f};

        const ve::rhi::RhiExtent2D extent = swapchain->GetExtent();

        if (!commandList->Begin() || !commandList->BeginRenderPass(*swapchain, renderPassDesc))
        {
            return false;
        }

        commandList->SetViewport(ve::rhi::RhiViewport{
            0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f});
        commandList->SetScissor(ve::rhi::RhiScissorRect{0, 0, extent.width, extent.height});
        commandList->SetPipeline(*pipelineState);
        commandList->SetVertexBuffer(0, *vertexBuffer, sizeof(TriangleVertex), 0);
        commandList->Draw(3, 0);
        commandList->EndRenderPass();

        if (!commandList->End() || !device->Submit(*commandList) || !swapchain->Present())
        {
            return false;
        }

        device->WaitIdle();
        return true;
    }
} // namespace

@interface VEngineMetalTriangleView : UIView

@end

@implementation VEngineMetalTriangleView

+ (Class)layerClass
{
    return [CAMetalLayer class];
}

@end

@interface VEngineMetalTriangleViewController : UIViewController

@end

@implementation VEngineMetalTriangleViewController
{
    BOOL rendered_;
}

- (void)loadView
{
    self.view = [[VEngineMetalTriangleView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
}

- (void)viewDidLayoutSubviews
{
    [super viewDidLayoutSubviews];

    if (rendered_)
    {
        return;
    }

    rendered_ = YES;

    CAMetalLayer* layer = (CAMetalLayer*)self.view.layer;
    const CGFloat scale = self.view.window.screen.scale;
    const CGSize drawableSize = CGSizeMake(self.view.bounds.size.width * scale, self.view.bounds.size.height * scale);
    layer.contentsScale = scale;

    RenderTriangle(layer, drawableSize);
}

@end

@interface VEngineMetalTriangleAppDelegate : UIResponder <UIApplicationDelegate>

@property(strong, nonatomic) UIWindow* window;

@end

@implementation VEngineMetalTriangleAppDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    (void)application;
    (void)launchOptions;

    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    self.window.rootViewController = [[VEngineMetalTriangleViewController alloc] init];
    [self.window makeKeyAndVisible];

    return YES;
}

@end

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([VEngineMetalTriangleAppDelegate class]));
    }
}
