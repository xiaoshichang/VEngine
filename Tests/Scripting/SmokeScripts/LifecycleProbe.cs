using VEngine;
using VEngine.Scripting;

namespace VEngine.SmokeScripts;

public sealed class LifecycleProbe : ScriptBehaviour
{
    private bool _updated;

    protected override void OnCreate()
    {
        Log.Info($"LifecycleProbe.OnCreate:{GameObject.Name}");
    }

    protected override void OnEnable()
    {
        Log.Info("LifecycleProbe.OnEnable");
    }

    protected override void OnUpdate(float deltaTime)
    {
        Vector3 position = Transform.LocalPosition;
        Transform.LocalPosition = new Vector3(position.X + deltaTime, position.Y, position.Z);

        if (!_updated)
        {
            Log.Info($"LifecycleProbe.OnUpdate:{Time.FrameIndex}:{Time.DeltaSeconds}");
            _updated = true;
        }
    }

    protected override void OnDisable()
    {
        Log.Info("LifecycleProbe.OnDisable");
    }

    protected override void OnDestroy()
    {
        Log.Info("LifecycleProbe.OnDestroy");
    }
}
