using VEngine.Scripting;

namespace VEngine;

public static class Log
{
    public static void Trace(string message)
    {
        ScriptBridge.Log(NativeLogSeverity.Trace, message);
    }

    public static void Debug(string message)
    {
        ScriptBridge.Log(NativeLogSeverity.Debug, message);
    }

    public static void Info(string message)
    {
        ScriptBridge.Log(NativeLogSeverity.Info, message);
    }

    public static void Warn(string message)
    {
        ScriptBridge.Log(NativeLogSeverity.Warn, message);
    }

    public static void Error(string message)
    {
        ScriptBridge.Log(NativeLogSeverity.Error, message);
    }
}
