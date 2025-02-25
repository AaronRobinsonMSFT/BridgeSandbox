using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Java;
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

public unsafe class JavaNode : BaseNode, INode
{
    private static int s_Counter = 0;
    private GCHandle _handle;
    private IntPtr* _jniHandlePtr;
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

        _jniHandlePtr = (IntPtr*)NativeMemory.Alloc((nuint)sizeof(void*));
        _jniHandlePtr[0] = instance;

#pragma warning disable CA1416 // Validate platform compatibility
        _handle = JavaMarshal.CreateReferenceTrackingHandle(this, (IntPtr)_jniHandlePtr);
#pragma warning restore CA1416 // Validate platform compatibility
    }

    ~JavaNode()
    {
        NativeMemory.Free(_jniHandlePtr);
        _handle.Free();
    }

    protected override IntPtr Handle
    {
        get => GCHandle.ToIntPtr(_handle);
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
        nint h = _jniHandlePtr[0];
        nint id = Handle;
        Console.WriteLine($"{prefix} {nameof(JavaNode)} {_id} {(h == IntPtr.Zero ? "Collected " : string.Empty)}({h:X}) Identity: {id:X}");

        foreach (object reference in _references)
        {
            if (reference is INode node)
            {
                node.Print("  " + prefix);
            }
        }
    }
}