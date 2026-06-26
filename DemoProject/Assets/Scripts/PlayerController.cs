using VEngine.Scripting;

namespace Game;

public sealed class PlayerController : ScriptComponent
{
    public float X = 0.0f;

    public override void OnUpdate(float deltaSeconds)
    {
        X += deltaSeconds;
        Transform.LocalPosition = new Vector3(0, 0, X);
    }
}
