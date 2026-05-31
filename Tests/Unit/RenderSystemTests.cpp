#include "Engine/Runtime/Render/RenderSystem.h"

#include <iostream>
#include <string_view>

namespace
{
    bool Expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool TestEditorViewportDefaultsToShaded()
    {
        const ve::EditorViewportRenderRequest request;
        return Expect(request.shaderMode == ve::EditorViewportShaderMode::Shaded,
                      "Editor viewport render requests should default to shaded mode.");
    }
} // namespace

int main()
{
    bool passed = true;
    passed &= TestEditorViewportDefaultsToShaded();

    if (passed)
    {
        std::cout << "VEngineRenderSystemTests passed" << '\n';
    }

    return passed ? 0 : 1;
}
