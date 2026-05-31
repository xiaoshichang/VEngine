#include "Engine/Runtime/Input/InputSystem.h"

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

    bool TestKeyTransitions()
    {
        ve::InputSystem input;

        input.BeginMainFrame();
        input.SubmitKey(ve::KeyCode::Space, true);
        input.ApplyGameSnapshot(input.CreateGameSnapshot());

        bool passed = true;
        passed &= Expect(input.GetKey(ve::KeyCode::Space), "Space should be held after key down.");
        passed &= Expect(input.GetKeyDown(ve::KeyCode::Space), "Space should report key down on the press frame.");
        passed &= Expect(!input.GetKeyUp(ve::KeyCode::Space), "Space should not report key up on the press frame.");

        input.BeginMainFrame();
        input.ApplyGameSnapshot(input.CreateGameSnapshot());
        passed &= Expect(input.GetKey(ve::KeyCode::Space), "Space should remain held on the next frame.");
        passed &= Expect(!input.GetKeyDown(ve::KeyCode::Space), "Space should not repeat key down without a new press.");

        input.BeginMainFrame();
        input.SubmitKey(ve::KeyCode::Space, false);
        input.ApplyGameSnapshot(input.CreateGameSnapshot());
        passed &= Expect(!input.GetKey(ve::KeyCode::Space), "Space should be released after key up.");
        passed &= Expect(input.GetKeyUp(ve::KeyCode::Space), "Space should report key up on the release frame.");
        return passed;
    }

    bool TestMouseAndFocusState()
    {
        ve::InputSystem input;

        input.BeginMainFrame();
        input.SetFocused(true);
        input.SetViewportSize(640, 360);
        input.SubmitMousePosition(10.0f, 20.0f);
        input.SubmitMouseButton(ve::MouseButton::Left, true);
        input.SubmitMouseWheel(1.0f);
        input.ApplyGameSnapshot(input.CreateGameSnapshot());

        bool passed = true;
        passed &= Expect(input.HasFocus(), "Input should report focus.");
        passed &= Expect(input.GetViewportWidth() == 640 && input.GetViewportHeight() == 360,
                         "Viewport size should be copied into the game snapshot.");
        passed &= Expect(input.GetMousePosition() == ve::Vector2(10.0f, 20.0f),
                         "Mouse position should be copied into the game snapshot.");
        passed &= Expect(input.GetMouseDelta() == ve::Vector2(10.0f, 20.0f),
                         "Mouse delta should accumulate from the previous frame.");
        passed &= Expect(input.GetMouseButtonDown(ve::MouseButton::Left),
                         "Left mouse should report button down on the press frame.");
        passed &= Expect(input.GetScrollDelta() == 1.0f, "Scroll delta should accumulate in the frame.");

        input.BeginMainFrame();
        input.SetFocused(false);
        input.ApplyGameSnapshot(input.CreateGameSnapshot());
        passed &= Expect(!input.HasFocus(), "Input should lose focus.");
        passed &= Expect(!input.GetMouseButton(ve::MouseButton::Left), "Losing focus should clear held mouse buttons.");
        return passed;
    }
} // namespace

int main()
{
    bool passed = true;
    passed &= TestKeyTransitions();
    passed &= TestMouseAndFocusState();
    return passed ? 0 : 1;
}
