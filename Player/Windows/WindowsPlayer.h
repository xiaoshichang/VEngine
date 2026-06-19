#pragma once

#include "Engine/Runtime/Application/Application.h"
#include "Engine/Runtime/Render/ViewportClient.h"
#include "Engine/Runtime/Resource/RuntimeAssetLoader.h"

#include <string>

namespace ve
{
    class WindowsPlayer final : public Application
    {
    public:
        explicit WindowsPlayer(ApplicationInitParam initParam);
        ~WindowsPlayer() override;

        [[nodiscard]] const ViewportClient& GetViewportClient() const noexcept;
        [[nodiscard]] ViewportClient& GetViewportClient() noexcept;

    protected:
        [[nodiscard]] ErrorCode InitializeRendering(Window& mainWindow) override;

    private:
        void RegisterSceneThreadViewportCallback();
        void HandleSceneThreadOSEvent(const OSEvent& event);
        void InitializePackagedProject();
        void LoadPendingPackagedStartupScene();

        ViewportClient viewportClient_;
        RuntimeAssetLoader runtimeAssetLoader_;
        std::string packagedStartScene_;
        bool pendingPackagedStartupSceneLoad_ = false;
    };
} // namespace ve
