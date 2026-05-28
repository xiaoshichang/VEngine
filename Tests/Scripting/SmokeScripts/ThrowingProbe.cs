using System;
using VEngine.Scripting;

namespace VEngine.SmokeScripts;

public sealed class ThrowingProbe : ScriptBehaviour
{
    protected override void OnUpdate(float deltaTime)
    {
        _ = deltaTime;
        throw new InvalidOperationException("ThrowingProbe update failure");
    }
}
