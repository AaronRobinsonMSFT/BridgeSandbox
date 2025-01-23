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

        Worker(root);

        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        GC.WaitForPendingFinalizers();

        // Use the supplied callback to call back into the Bridge.
        s_BridgeContext->Callback(s_BridgeContext);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void Worker(JavaNode root)
    {
        INode c1 = CreateBranch();
        root.AddReference(c1);

        root.Print("1-");

        {
            using Marshaller marshaller = new(root.BuildJavaReferenceGraph());
            s_BridgeContext->SetObjectGraph(marshaller.Length, (void*)marshaller.Ptr);
            GC.Collect();
        }

        Console.WriteLine($"Mark collectible nodes");
        {
            using Marshaller marshaller = new(root.BuildJavaReferenceGraph(c1));
            s_BridgeContext->SetObjectGraph(marshaller.Length, (void*)marshaller.Ptr);
            GC.Collect();
        }

        root.Print("2-");

        Console.WriteLine("Prune .NET references");
        root.PruneReferences();

        root.Print("3-");

        [MethodImpl(MethodImplOptions.NoInlining)]
        static INode CreateBranch()
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

    private struct Marshaller : IDisposable
    {
        public int Length { get; }
        public IntPtr Ptr { get; }

        public Marshaller(JavaReferences[] allReferences)
        {
            Length = allReferences.Length;
            var res = (JavaReferencesUnmanaged*)NativeMemory.Alloc((nuint)(sizeof(JavaReferencesUnmanaged) * Length));
            Ptr = (IntPtr)res;

            foreach (var refs in allReferences)
            {
                res->Object = refs.Object;
                res->ObjectManagedLifetime = Init.s_JavaWrappers.GetOrCreateComInterfaceForObject(refs.ObjectManagedLifetime, CreateComInterfaceFlags.TrackerSupport);
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
                Marshal.Release(refs.Object);
                Marshal.Release(refs.ObjectManagedLifetime);
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
            public IntPtr Object;
            public IntPtr ObjectManagedLifetime;
            public IntPtr* References;
            public byte Collectible;
        }
    }
}
