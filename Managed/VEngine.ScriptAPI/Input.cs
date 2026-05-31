using VEngine;

namespace VEngine.Scripting;

public enum KeyCode
{
    Unknown = 0,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    Space,
    Enter,
    Escape,
    Tab,
    Backspace,
    Left,
    Right,
    Up,
    Down,
    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    LeftAlt,
    RightAlt,
}

public enum MouseButton
{
    Left = 0,
    Right,
    Middle,
    X1,
    X2,
}

public static class Input
{
    public static Vector2 MousePosition => ScriptBridge.GetMousePosition();

    public static Vector2 MouseDelta => ScriptBridge.GetMouseDelta();

    public static float ScrollDelta => ScriptBridge.GetScrollDelta();

    public static bool GetKey(KeyCode keyCode)
    {
        return ScriptBridge.GetKey(keyCode);
    }

    public static bool GetKeyDown(KeyCode keyCode)
    {
        return ScriptBridge.GetKeyDown(keyCode);
    }

    public static bool GetKeyUp(KeyCode keyCode)
    {
        return ScriptBridge.GetKeyUp(keyCode);
    }

    public static bool GetMouseButton(MouseButton mouseButton)
    {
        return ScriptBridge.GetMouseButton(mouseButton);
    }

    public static bool GetMouseButtonDown(MouseButton mouseButton)
    {
        return ScriptBridge.GetMouseButtonDown(mouseButton);
    }

    public static bool GetMouseButtonUp(MouseButton mouseButton)
    {
        return ScriptBridge.GetMouseButtonUp(mouseButton);
    }
}
