using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace DnLib;

public class DotnetNode : BaseNode, INode
{
    public void AddReference(object obj)
    {
        _references.Add(obj);
    }

    public void ClearReferences()
    {
        _references.Clear();
    }

    public void Print(string prefix)
    {
        Console.WriteLine($"{prefix} {nameof(DotnetNode)}");
        foreach (object reference in _references)
        {
            if (reference is INode node)
            {
                node.Print("  " + prefix);
            }
        }
    }
}

public unsafe class JavaNode : BaseNode, INode, IJVMObject
{
    private static int s_Counter = 0;
    private IntPtr _instanceRaw;
    private readonly int _id;

    public JavaNode()
    {
        _id = ++s_Counter;

        int hr;
        IntPtr instance;
        ReadOnlySpan<byte> className = "Node"u8;
        fixed (byte* ptr = &ReadOnlySpanMarshaller<byte, byte>.ManagedToUnmanagedIn.GetPinnableReference(className))
        {
            hr = Init.s_BridgeContext->CreateObject(ptr, _id, (void**)&instance);
            Marshal.ThrowExceptionForHR(hr, (IntPtr)(-1));
        }

        hr = Marshal.QueryInterface(instance, typeof(IJVMObject).GUID, out nint jvmObjectInst);
        if (hr != 0)
        {
            throw new NotSupportedException("Only supports IJVMObject");
        }
        int rc = Marshal.Release(instance);
        Debug.Assert(rc == 1);
        Debug.Assert(jvmObjectInst != 0);

        _instanceRaw = jvmObjectInst;
        Init.s_JavaWrappers.GetOrRegisterObjectForComInstance(_instanceRaw, CreateObjectFlags.TrackerObject, this, IntPtr.Zero);
    }

    ~JavaNode()
    {
        Marshal.Release(_instanceRaw);
    }

    protected override IntPtr Handle
    {
        get
        {
            JavaWrappers.TryGetComInstance(this, out IntPtr ptr);
            return ptr;
        }
    }

    public void AddReference(object obj)
    {
        _references.Add(obj);
    }

    public void ClearReferences()
    {
        _references.Clear();
    }

    public void Print(string prefix)
    {
        nint h = GetJNIHandle();
        nint id = Handle;
        Console.WriteLine($"{prefix} {nameof(JavaNode)} {_id} {(h == IntPtr.Zero ? "Collected " : string.Empty)}({h:X}) Identity: {id:X}");
        Marshal.Release(id);

        foreach (object reference in _references)
        {
            if (reference is INode node)
            {
                node.Print("  " + prefix);
            }
        }
    }

    public IntPtr GetJNIHandle()
    {
        var fptr = ((delegate* unmanaged[MemberFunction]<IntPtr, IntPtr> )(*(*(void***)_instanceRaw + 3)));
        IntPtr handle = fptr(_instanceRaw);
        GC.KeepAlive(this);
        return handle;
    }
}