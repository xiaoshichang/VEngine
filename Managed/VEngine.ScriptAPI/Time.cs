using VEngine.Scripting;

namespace VEngine;

public static class Time
{
    public static float DeltaSeconds => ScriptBridge.GetDeltaSeconds();

    public static double TotalSeconds => ScriptBridge.GetTotalSeconds();

    public static ulong FrameIndex => ScriptBridge.GetFrameIndex();
}
