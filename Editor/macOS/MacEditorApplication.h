#pragma once

#include "Engine/Runtime/Application/Application.h"

namespace ve::editor
{
    class MacEditorApplication : public ve::Application
    {
    public:
        explicit MacEditorApplication(ve::ApplicationInitParam initParam);
        ~MacEditorApplication() override;

        [[nodiscard]] int Init() override;
        void Run() override;
        void UnInit() override;
    };
} // namespace ve::editor
