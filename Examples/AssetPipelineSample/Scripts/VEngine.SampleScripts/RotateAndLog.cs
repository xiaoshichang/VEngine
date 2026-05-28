using VEngine;
using VEngine.Scripting;

namespace VEngine.SampleScripts;

public sealed class RotateAndLog : ScriptBehaviour
{
    protected override void OnCreate()
    {
        Log.Info($"RotateAndLog attached to {GameObject.Name}");
    }

    protected override void OnUpdate(float deltaTime)
    {
        _ = deltaTime;
        Transform.LocalRotation = Quaternion.FromEulerXYZ(0.0f, (float)Time.TotalSeconds * 1.2f, 0.0f);
    }
}
