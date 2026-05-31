using VEngine;

namespace VEngine.Scripting;

public static class Physics
{
    public static bool Raycast(Ray ray, out RaycastHit hit)
    {
        return ScriptBridge.Raycast(ray, out hit, ulong.MaxValue, false);
    }

    public static bool Raycast(Ray ray, out RaycastHit hit, ulong queryMask, bool includeTriggers)
    {
        return ScriptBridge.Raycast(ray, out hit, queryMask, includeTriggers);
    }
}
