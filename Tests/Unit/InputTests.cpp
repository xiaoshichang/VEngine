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

    bool TestMergedSnapshotsPreserveClickUntilConsumed()
    {
        ve::InputSystem input;

        input.BeginMainFrame();
        input.SetFocused(true);
        input.SubmitMouseButton(ve::MouseButton::Left, true);
        ve::InputSnapshot pending = input.CreateGameSnapshot();

        input.BeginMainFrame();
        ve::InputSnapshot next = input.CreateGameSnapshot();
        ve::InputSnapshot merged = ve::InputSystem::MergeSnapshots(pending, next);
        input.ApplyGameSnapshot(merged);

        bool passed = true;
        passed &= Expect(input.GetMouseButton(ve::MouseButton::Left),
                         "Merged snapshot should keep the latest held mouse state.");
        passed &= Expect(input.GetMouseButtonDown(ve::MouseButton::Left),
                         "Merged snapshot should preserve a click until the Game Thread consumes it.");
        return passed;
    }

    bool TestHeldMouseButtonDoesNotRepeatDown()
    {
        ve::InputSystem input;

        input.BeginMainFrame();
        input.SetFocused(true);
        input.SubmitMouseButton(ve::MouseButton::Left, true);
        input.ApplyGameSnapshot(input.CreateGameSnapshot());

        input.BeginMainFrame();
        input.SetFocused(true);
        input.SubmitMouseButton(ve::MouseButton::Left, true);
        input.ApplyGameSnapshot(input.CreateGameSnapshot());

        bool passed = true;
        passed &= Expect(input.GetMouseButton(ve::MouseButton::Left),
                         "Held mouse button should remain down on the next frame.");
        passed &= Expect(!input.GetMouseButtonDown(ve::MouseButton::Left),
                         "Held mouse button should not repeat button down on the next frame.");
        return passed;
    }

    bool TestTransientGameInputClearsWithoutNewSnapshot()
    {
        ve::InputSystem input;

        input.BeginMainFrame();
        input.SetFocused(true);
        input.SubmitMouseButton(ve::MouseButton::Left, true);
        input.SubmitMouseWheel(1.0f);
        input.ApplyGameSnapshot(input.CreateGameSnapshot());

        bool passed = true;
        passed &= Expect(input.GetMouseButtonDown(ve::MouseButton::Left),
                         "Mouse button down should be visible on the consumed game frame.");
        passed &= Expect(input.GetScrollDelta() == 1.0f, "Scroll delta should be visible on the consumed game frame.");

        input.ClearGameTransientState();
        passed &= Expect(input.GetMouseButton(ve::MouseButton::Left),
                         "Clearing transient game input should preserve held mouse state.");
        passed &= Expect(!input.GetMouseButtonDown(ve::MouseButton::Left),
                         "Mouse button down should not repeat without a new input snapshot.");
        passed &= Expect(input.GetScrollDelta() == 0.0f,
                         "Scroll delta should not repeat without a new input snapshot.");
        return passed;
    }

    bool TestExplicitMouseButtonEventsDoNotRepeatAfterFocusClear()
    {
        ve::InputSystem input;

        input.BeginMainFrame();
        input.SetFocused(true);
        input.SubmitMouseButtonState(ve::MouseButton::Left, true, true, false);
        input.ApplyGameSnapshot(input.CreateGameSnapshot());

        bool passed = true;
        passed &= Expect(input.GetMouseButtonDown(ve::MouseButton::Left),
                         "Explicit mouse click should report button down.");

        input.BeginMainFrame();
        input.SetFocused(false);
        input.SetFocused(true);
        input.SubmitMouseButtonState(ve::MouseButton::Left, true, false, false);
        input.ApplyGameSnapshot(input.CreateGameSnapshot());

        passed &= Expect(input.GetMouseButton(ve::MouseButton::Left),
                         "Explicit held state should restore held mouse state after a focus clear.");
        passed &= Expect(!input.GetMouseButtonDown(ve::MouseButton::Left),
                         "Explicit held state should not synthesize another button down after a focus clear.");
        return passed;
    }
} // namespace

int main()
{
    bool passed = true;
    passed &= TestKeyTransitions();
    passed &= TestMouseAndFocusState();
    passed &= TestMergedSnapshotsPreserveClickUntilConsumed();
    passed &= TestHeldMouseButtonDoesNotRepeatDown();
    passed &= TestTransientGameInputClearsWithoutNewSnapshot();
    passed &= TestExplicitMouseButtonEventsDoNotRepeatAfterFocusClear();
    return passed ? 0 : 1;
}
