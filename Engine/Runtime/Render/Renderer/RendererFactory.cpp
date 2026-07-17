#include "Engine/Runtime/Render/Renderer/RendererFactory.h"

#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/Render/Renderer/MobileRenderer.h"
#include "Engine/Runtime/Render/Renderer/StandaloneRenderer.h"

#include <utility>

namespace ve
{
    std::unique_ptr<BaseRenderer> CreatePlayerRenderer(BaseRendererInitParam initParam)
    {
#if VE_PLATFORM_IOS
        MobileRendererInitParam mobileInitParam = {};
        static_cast<BaseRendererInitParam&>(mobileInitParam) = std::move(initParam);
        return std::make_unique<MobileRenderer>(std::move(mobileInitParam));
#elif VE_PLATFORM_WINDOWS || VE_PLATFORM_MACOS
        StandaloneRendererInitParam standaloneInitParam = {};
        static_cast<BaseRendererInitParam&>(standaloneInitParam) = std::move(initParam);
        return std::make_unique<StandaloneRenderer>(std::move(standaloneInitParam));
#else
#error Unsupported player renderer platform.
#endif
    }
} // namespace ve
