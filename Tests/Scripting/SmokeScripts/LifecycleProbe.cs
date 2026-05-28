using VEngine;
using VEngine.Scripting;

namespace VEngine.SmokeScripts;

public sealed class LifecycleProbe : ScriptBehaviour
{
    protected override void OnCreate()
    {
        Log.Info($"LifecycleProbe created on {GameObject.Name}");
    }

    protected override void OnUpdate(float deltaTime)
    {
        Vector3 position = Transform.LocalPosition;
        Transform.LocalPosition = new Vector3(position.X + deltaTime, position.Y, position.Z);
    }
}
