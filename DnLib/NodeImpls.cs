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
    private IntPtr* _jniHandlePtr;
    private readonly int _id;

    public static void ReleaseContext(IntPtr context)
    {
        NativeMemory.Free((void*)context);
    }

    public JavaNode()
    {
        _id = ++s_Counter;

        int hr;
        IntPtr instance;
        int instanceId;
        ReadOnlySpan<byte> className = "Node"u8;
        fixed (byte* ptr = &ReadOnlySpanMarshaller<byte, byte>.ManagedToUnmanagedIn.GetPinnableReference(className))
        {
            hr = Init.s_BridgeContext->CreateObject(ptr, _id, (void**)&instance, &instanceId);
            Marshal.ThrowExceptionForHR(hr, (IntPtr)(-1));
        }

        _jniHandlePtr = (IntPtr*)NativeMemory.Alloc(((nuint)sizeof(void*) * 2));
        _jniHandlePtr[0] = instance;
        _jniHandlePtr[1] = instanceId;

#pragma warning disable CA1416 // Validate platform compatibility
        GCHandle handle = JavaMarshal.CreateReferenceTrackingHandle(this, (IntPtr)_jniHandlePtr);
#pragma warning restore CA1416 // Validate platform compatibility

        HandleMap.Instance.Add(instanceId, handle);
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
        nint id = _jniHandlePtr[1];
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