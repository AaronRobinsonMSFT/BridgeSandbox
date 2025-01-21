using System.Collections;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace DnLib;

// BridgeContext defined in bridge.cpp.
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct BridgeContext
{
    public void* Jvmti;
    public void* JNIEnv;
    public delegate* unmanaged[Cdecl]<void*, void> Callback; // Cdecl is needed for x86 scenarios. Ignored on other platforms.
    public delegate* unmanaged[Cdecl]<byte*, int, void**, int> CreateObject;
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

        INode c1 = CreateTrimmableBranch();
        root.AddReference(c1);

        root.Print("1-");

        {
            using Marshaler marshaler = new(root.BuildJavaReferenceGraph());
            s_BridgeContext->SetObjectGraph(marshaler.Length, (void*)marshaler.Ptr);
            GC.Collect();
        }

        Console.WriteLine($"Manually mark collectible nodes");
        {
            using Marshaler marshaler = new(root.BuildJavaReferenceGraph(c1));
            s_BridgeContext->SetObjectGraph(marshaler.Length, (void*)marshaler.Ptr);
            GC.Collect();
        }

        root.Print("2-");

        Console.WriteLine("Manually prune .NET references");
        root.PruneReferences();

        root.Print("3-");

        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        GC.WaitForPendingFinalizers();

        // Use the supplied callback to call back into the Bridge.
        s_BridgeContext->Callback(s_BridgeContext);

        [MethodImpl(MethodImplOptions.NoInlining)]
        static INode CreateTrimmableBranch()
        {
            return Create<DotnetNode>(
                Create<JavaNode>(
                    Create<DotnetNode>(),
                    Create<JavaNode>(),
                    Create<JavaNode>()
                ),
                Create<DotnetNode>(
                    Create<DotnetNode>(
                        Create<JavaNode>(),
                        Create<JavaNode>()
                    ),
                    Create<JavaNode>(),
                    Create<JavaNode>(),
                    Create<JavaNode>()
                ),
                Create<JavaNode>()
            );
        }
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
                res->Collectible = refs.Collectible ? (byte)1 : (byte)0;
                res++;
            }
        }

        public void Dispose()
        {
            var res = (JavaReferencesUnmanaged*)Ptr;
            foreach (var refs in new Span<JavaReferencesUnmanaged>((void*)Ptr, Length))
            {
                Marshal.Release(refs.Handle);
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
            public byte Collectible;
        }
    }
}
