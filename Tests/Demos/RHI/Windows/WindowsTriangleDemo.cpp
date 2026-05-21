#include "Tests/Demos/RHI/Windows/WindowsTriangleDemo.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <array>
#include <iostream>

namespace ve::tests
{
namespace
{
constexpr uint32_t DemoWidth = 800;
constexpr uint32_t DemoHeight = 600;

struct TriangleVertex
{
    float position[3] = {};
};

const char* TriangleShaderSource = R"(
struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0f);
    output.color = float3(input.position.x + 0.5f, input.position.y + 0.5f, 1.0f);
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return float4(input.color, 1.0f);
}
)";

LRESULT CALLBACK DemoWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

HWND CreateDemoWindow(const char* title)
{
    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = DemoWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"VEngineRhiTriangleDemoWindow";

    RegisterClassExW(&windowClass);

    RECT windowRect = {0, 0, static_cast<LONG>(DemoWidth), static_cast<LONG>(DemoHeight)};
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    wchar_t wideTitle[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wideTitle, static_cast<int>(std::size(wideTitle)));

    HWND window = CreateWindowExW(
        0,
        windowClass.lpszClassName,
        wideTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (window != nullptr)
    {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    }

    return window;
}

bool PumpMessages()
{
    MSG message = {};

    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
        {
            return false;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return true;
}

void PrintRhiError(const char* operation, const rhi::RhiDevice& device)
{
    std::cerr << operation << " failed: " << device.GetLastErrorMessage() << '\n';
}
}

int RunWindowsTriangleDemo(const char* title, std::unique_ptr<rhi::RhiDevice> device, uint32_t maxFrames)
{
    if (device == nullptr)
    {
        std::cerr << "Failed to create RHI device." << '\n';
        return 1;
    }

    HWND window = CreateDemoWindow(title);

    if (window == nullptr)
    {
        std::cerr << "Failed to create demo window." << '\n';
        return 1;
    }

    rhi::RhiSwapchainDesc swapchainDesc = {};
    swapchainDesc.nativeWindow = window;
    swapchainDesc.width = DemoWidth;
    swapchainDesc.height = DemoHeight;
    swapchainDesc.colorFormat = rhi::RhiFormat::Bgra8Unorm;
    swapchainDesc.bufferCount = 2;
    swapchainDesc.debugName = title;

    std::unique_ptr<rhi::RhiSwapchain> swapchain = device->CreateSwapchain(swapchainDesc);

    if (swapchain == nullptr)
    {
        PrintRhiError("CreateSwapchain", *device);
        return 1;
    }

    constexpr std::array<TriangleVertex, 3> vertices = {
        TriangleVertex{{0.0f, 0.5f, 0.0f}},
        TriangleVertex{{0.5f, -0.5f, 0.0f}},
        TriangleVertex{{-0.5f, -0.5f, 0.0f}},
    };

    rhi::RhiBufferDesc vertexBufferDesc = {};
    vertexBufferDesc.size = sizeof(TriangleVertex) * vertices.size();
    vertexBufferDesc.usage = rhi::RhiBufferUsage::Vertex;
    vertexBufferDesc.initialData = vertices.data();
    vertexBufferDesc.debugName = "TriangleVertexBuffer";

    std::unique_ptr<rhi::RhiBuffer> vertexBuffer = device->CreateBuffer(vertexBufferDesc);

    if (vertexBuffer == nullptr)
    {
        PrintRhiError("CreateBuffer", *device);
        return 1;
    }

    rhi::RhiShaderModuleDesc vertexShaderDesc = {};
    vertexShaderDesc.stage = rhi::RhiShaderStage::Vertex;
    vertexShaderDesc.source = TriangleShaderSource;
    vertexShaderDesc.entryPoint = "VSMain";
    vertexShaderDesc.debugName = "TriangleVertexShader";

    std::unique_ptr<rhi::RhiShaderModule> vertexShader = device->CreateShaderModule(vertexShaderDesc);

    if (vertexShader == nullptr)
    {
        PrintRhiError("CreateShaderModule VS", *device);
        return 1;
    }

    rhi::RhiShaderModuleDesc fragmentShaderDesc = {};
    fragmentShaderDesc.stage = rhi::RhiShaderStage::Fragment;
    fragmentShaderDesc.source = TriangleShaderSource;
    fragmentShaderDesc.entryPoint = "PSMain";
    fragmentShaderDesc.debugName = "TriangleFragmentShader";

    std::unique_ptr<rhi::RhiShaderModule> fragmentShader = device->CreateShaderModule(fragmentShaderDesc);

    if (fragmentShader == nullptr)
    {
        PrintRhiError("CreateShaderModule PS", *device);
        return 1;
    }

    rhi::RhiVertexAttributeDesc positionAttribute = {};
    positionAttribute.semanticName = "POSITION";
    positionAttribute.semanticIndex = 0;
    positionAttribute.format = rhi::RhiFormat::Rgb32Float;
    positionAttribute.offset = 0;

    rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
    pipelineDesc.vertexShader = vertexShader.get();
    pipelineDesc.fragmentShader = fragmentShader.get();
    pipelineDesc.vertexLayout.attributes = &positionAttribute;
    pipelineDesc.vertexLayout.attributeCount = 1;
    pipelineDesc.vertexLayout.stride = sizeof(TriangleVertex);
    pipelineDesc.topology = rhi::RhiPrimitiveTopology::TriangleList;
    pipelineDesc.colorFormat = swapchain->GetColorFormat();
    pipelineDesc.debugName = "TrianglePipeline";

    std::unique_ptr<rhi::RhiPipelineState> pipelineState = device->CreateGraphicsPipeline(pipelineDesc);

    if (pipelineState == nullptr)
    {
        PrintRhiError("CreateGraphicsPipeline", *device);
        return 1;
    }

    std::unique_ptr<rhi::RhiCommandList> commandList = device->CreateCommandList();

    if (commandList == nullptr)
    {
        PrintRhiError("CreateCommandList", *device);
        return 1;
    }

    bool running = true;
    uint32_t renderedFrames = 0;

    while (running)
    {
        running = PumpMessages();

        if (!running)
        {
            break;
        }

        rhi::RhiRenderPassDesc renderPassDesc = {};
        renderPassDesc.colorLoadAction = rhi::RhiLoadAction::Clear;
        renderPassDesc.colorStoreAction = rhi::RhiStoreAction::Store;
        renderPassDesc.clearColor = {0.05f, 0.07f, 0.10f, 1.0f};

        const rhi::RhiExtent2D extent = swapchain->GetExtent();

        if (!commandList->Begin() || !commandList->BeginRenderPass(*swapchain, renderPassDesc))
        {
            std::cerr << "Failed to begin RHI command list or render pass." << '\n';
            return 1;
        }

        commandList->SetViewport(
            rhi::RhiViewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f});
        commandList->SetScissor(rhi::RhiScissorRect{0, 0, extent.width, extent.height});
        commandList->SetPipeline(*pipelineState);
        commandList->SetVertexBuffer(0, *vertexBuffer, sizeof(TriangleVertex), 0);
        commandList->Draw(3, 0);
        commandList->EndRenderPass();

        if (!commandList->End() || !device->Submit(*commandList) || !swapchain->Present())
        {
            PrintRhiError("Submit/Present", *device);
            return 1;
        }

        ++renderedFrames;

        if (maxFrames > 0 && renderedFrames >= maxFrames)
        {
            running = false;
        }
    }

    device->WaitIdle();
    return 0;
}
}
