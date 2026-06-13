#pragma once

#include "Engine/Runtime/Application/Application.h"
#include "Engine/Runtime/Render/ViewportClient.h"

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
        void OnMainLoopIteration(Window& mainWindow) override;

    private:
        ViewportClient viewportClient_;
    };
} // namespace ve
