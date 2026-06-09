#pragma once

#include "Engine/Runtime/Application/Application.h"

namespace ve::editor
{
    class WindowsEditorApplication : public ve::Application
    {
    public:
        explicit WindowsEditorApplication(ve::ApplicationInitParam initParam);
        ~WindowsEditorApplication() override;

        [[nodiscard]] int Init() override;
        void Run() override;
        void UnInit() override;
    };
} // namespace ve::editor
