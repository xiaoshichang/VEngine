#pragma once

#include "Editor/Core/Editor.h"
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

    private:
        Editor editor_;
    };
} // namespace ve::editor
