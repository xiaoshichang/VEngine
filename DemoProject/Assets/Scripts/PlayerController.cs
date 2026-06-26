using VEngine.Scripting;

namespace Game;

public sealed class PlayerController : ScriptComponent
{
    private float x = 0;
    public override void OnUpdate(float deltaSeconds)
    {
        x += deltaSeconds;
        Transform.LocalPosition = new Vector3(0, 0, x);
    }
}
