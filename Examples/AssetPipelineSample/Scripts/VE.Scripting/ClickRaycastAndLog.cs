using VEngine;
using VEngine.Scripting;

namespace VE.Scripting;

public sealed class ClickRaycastAndLog : ScriptBehaviour
{
    protected override void OnCreate()
    {
        Log.Info($"ClickRaycastAndLog attached to {GameObject.Name}");
    }

    protected override void OnUpdate(float deltaTime)
    {
        _ = deltaTime;

        if (!Input.GetMouseButtonDown(MouseButton.Left))
        {
            return;
        }

        Camera? camera = Camera.Main;
        if (camera == null)
        {
            Log.Warn("Click raycast skipped because no active Camera was found.");
            return;
        }

        Ray ray = camera.ScreenPointToRay(Input.MousePosition);
        if (Physics.Raycast(ray, out RaycastHit hit, ulong.MaxValue, true))
        {
            Log.Info($"Clicked {hit.GameObject.Name}, {hit.Position}");
        }
        else
        {
            Log.Info("Clicked empty space");
        }
    }
}
