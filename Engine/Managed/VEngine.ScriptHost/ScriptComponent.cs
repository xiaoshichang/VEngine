namespace VEngine.Scripting;

public abstract class ScriptComponent
{
    internal nint NativeComponent { get; set; }

    public Transform Transform => new(NativeComponent);
    public Camera Camera => new(NativeComponent);
    public Light Light => new(NativeComponent);

    public virtual void OnCreate()
    {
    }

    public virtual void OnDestroy()
    {
    }

    public virtual void OnEnable()
    {
    }

    public virtual void OnDisable()
    {
    }

    public virtual void OnUpdate(float deltaSeconds)
    {
    }

    public virtual void OnLateUpdate(float deltaSeconds)
    {
    }
}
