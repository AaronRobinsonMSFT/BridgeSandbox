using System.Collections;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace DnLib;

// BridgeContext defined in bridge.cpp.
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeContext
{
    public void* Jvmti;
    public void* JNIEnv;
    public delegate* unmanaged[Cdecl]<void*, void> Callback; // Cdecl is needed for x86 scenarios. Ignored on other platforms.
    public delegate* unmanaged[Cdecl]<byte*, void*, void**, int> CreateObject;
    public delegate* unmanaged[Cdecl]<int, void*, void> SetObjectGraph;
}

public unsafe sealed class Init
{
    internal static JavaWrappers s_JavaWrappers = new JavaWrappers();
    internal static BridgeContext* s_BridgeContext;

    [UnmanagedCallersOnly(
        EntryPoint = "Java_JavaApp_DotnetInit",             // Defined name by JNI. See Java build for generated header file.
        CallConvs = new Type[] { typeof(CallConvCdecl) })   // Cdecl is needed to match JNI signature on x86. Ignored on other platforms.
    ]
    public static void Initialize(nint jniEnv, nint _, void* bridgeContextRaw)
    {
        Console.WriteLine("DnLib!DnLib.Init.Initialize()");

        s_BridgeContext = (BridgeContext*)bridgeContextRaw;

        // Use the supplied callback to call back into the Bridge.
        s_BridgeContext->Callback(s_BridgeContext);
    }

    [UnmanagedCallersOnly(
        EntryPoint = "Java_JavaApp_DotnetMain",
        CallConvs = new Type[] { typeof(CallConvCdecl) })
    ]
    public static void Main(nint jniEnv)
    {
        Console.WriteLine("DnLib!DnLib.Init.Main()");

        // Create a new instance of JavaNode.
        JavaNode root = Create<JavaNode>();
        root.AddReference(
            Create<DotnetNode>(
                Create<JavaNode>()
            )
        );
        root.AddReference(
            Create<DotnetNode>(
                Create<JavaNode>()
            )
        );

        root.Print("|-");

        Marshaler marshaler = new(root.BuildJavaReferenceGraph());
        try
        {
            s_BridgeContext->SetObjectGraph(marshaler.Length, (void*)marshaler.Ptr);
        }
        finally
        {
            marshaler.Dispose();
        }

        GC.Collect();
        GC.WaitForPendingFinalizers();

        root.Print("|-");
    }

    public static T Create<T>(params INode[] children) where T: INode, new()
    {
        // Create a new instance of T.
        T obj = new();

        // Add references to the object.
        foreach (INode node in children)
        {
            obj.AddReference(node);
        }

        return obj;
    }

    private struct Marshaler : IDisposable
    {
        public int Length { get; }
        public IntPtr Ptr { get; }

        public Marshaler(JavaReferences[] allReferences)
        {
            var res = (JavaReferencesUnmanaged*)NativeMemory.Alloc((nuint)(sizeof(JavaReferencesUnmanaged) * allReferences.Length));
            Ptr = (IntPtr)res;
            Length = allReferences.Length;

            foreach (var refs in allReferences)
            {
                res->Handle = refs.Handle;
                int refLen = refs.References.Length;
                res->References = (IntPtr*)NativeMemory.Alloc((nuint)(sizeof(IntPtr) * (refLen + 1)));
                refs.References.CopyTo(new Span<IntPtr>(res->References, refLen));
                res->References[refLen] = IntPtr.Zero; // Null-terminate the array.
                res++;
            }
        }

        public void Dispose()
        {
            var res = (JavaReferencesUnmanaged*)Ptr;
            foreach (var refs in new Span<JavaReferencesUnmanaged>((void*)Ptr, Length))
            {
                for (int i = 0; refs.References[i] != IntPtr.Zero; i++)
                {
                    Marshal.Release(refs.References[i]);
                }
                NativeMemory.Free((void*)refs.References);
            }
            NativeMemory.Free((void*)Ptr);
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct JavaReferencesUnmanaged
        {
            public IntPtr Handle;
            public IntPtr* References;
        }
    }
}

[Guid("329b458a-98cf-41b2-80b4-4f3254af50b2")]
public interface IJVMObject
{
    IntPtr GetJNIHandle();
}

[StructLayout(LayoutKind.Sequential)]
public struct JavaReferences
{
    public IntPtr Handle;
    public IntPtr[] References;

    public override string ToString()
    {
        return $"Handle: {Handle:X}, References: {string.Join(", ", References.Select(r => r.ToString("X")))}";
    }
}

public abstract class BaseNode
{
    protected List<object> _references = new List<object>();

    protected virtual IntPtr Handle { get; }

    public JavaReferences[] BuildJavaReferenceGraph()
    {
        IntPtr handle = Handle;

        List<IntPtr> directs = new();
        List<JavaReferences> references = new();
        foreach (object inst in _references)
        {
            JavaReferences[] refs;
            if (inst is JavaNode javaNode)
            {
                directs.Add(javaNode.Handle);
                refs = javaNode.BuildJavaReferenceGraph();
            }
            else if (inst is DotnetNode dnNode)
            {
                refs = dnNode.BuildJavaReferenceGraph();
            }
            else
            {
                throw new InvalidOperationException("Unknown node type.");
            }

            Debug.Assert(refs.Length >= 1);
            ref JavaReferences direct = ref refs[0];
            if (direct.Handle == IntPtr.Zero)
            {
                // Fold the direct references into the current node.
                directs.AddRange(direct.References);
                references.AddRange(refs.Skip(1));
            }
            else
            {
                references.AddRange(refs);
            }
        }

        return references.Prepend(new JavaReferences { Handle = handle, References = directs.ToArray() }).ToArray();
    }
}

public interface INode
{
    void AddReference(object obj);
    void ClearReferences();

    // Utility methods for prototyping.
    void Print(string prefix);
}

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
    private IJVMObject _jvmObj;

    public JavaNode()
    {
        void* instance;
        ReadOnlySpan<byte> className = "Node"u8;
        fixed (byte* ptr = &ReadOnlySpanMarshaller<byte, byte>.ManagedToUnmanagedIn.GetPinnableReference(className))
        {
            int hr = Init.s_BridgeContext->CreateObject(ptr, null, &instance);
            Marshal.ThrowExceptionForHR(hr);
        }

        _jvmObj = (IJVMObject)Init.s_JavaWrappers.GetOrCreateObjectForComInstance((IntPtr)instance, CreateObjectFlags.TrackerObject);
        Marshal.Release((IntPtr)instance);

        Console.WriteLine($"JNI Handle: {_jvmObj.GetJNIHandle():X}");
    }

    protected override IntPtr Handle
    {
        get
        {
            ComWrappers.TryGetComInstance(_jvmObj, out IntPtr ptr);
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
        Console.WriteLine($"{prefix} {nameof(JavaNode)} (0x{_jvmObj.GetJNIHandle():x})");
        foreach (object reference in _references)
        {
            if (reference is INode node)
            {
                node.Print("  " + prefix);
            }
        }
    }
}
