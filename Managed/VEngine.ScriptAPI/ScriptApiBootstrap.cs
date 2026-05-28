using System;

namespace VEngine.Scripting;

public static class ScriptApiBootstrap
{
    public static int Initialize(IntPtr argument, int argumentSizeInBytes)
    {
        _ = argument;
        _ = argumentSizeInBytes;
        return 0;
    }
}
