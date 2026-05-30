namespace VEngine.Scripting;

public abstract class ScriptBehaviour : Component
{
    protected ScriptBehaviour()
    {
    }

    internal void InvokeOnCreate()
    {
        OnCreate();
    }

    internal void InvokeOnDestroy()
    {
        OnDestroy();
    }

    internal void InvokeOnEnable()
    {
        OnEnable();
    }

    internal void InvokeOnDisable()
    {
        OnDisable();
    }

    internal void InvokeOnUpdate(float deltaTime)
    {
        OnUpdate(deltaTime);
    }

    internal void InvokeOnFixedUpdate(float fixedDeltaTime)
    {
        OnFixedUpdate(fixedDeltaTime);
    }

    protected virtual void OnCreate()
    {
    }

    protected virtual void OnDestroy()
    {
    }

    protected virtual void OnEnable()
    {
    }

    protected virtual void OnDisable()
    {
    }

    protected virtual void OnUpdate(float deltaTime)
    {
    }

    protected virtual void OnFixedUpdate(float fixedDeltaTime)
    {
    }
}
