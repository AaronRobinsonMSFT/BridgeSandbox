using System.Collections;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Java;

namespace DnLib;

// BridgeContext defined in bridge.cpp.
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct BridgeContext
{
    public void* Jvmti;
    public void* JNIEnv;
    public delegate* unmanaged[Cdecl]<void*, void> Callback; // Cdecl is needed for x86 scenarios. Ignored on other platforms.
    public delegate* unmanaged[Cdecl]<void> InitializeBridge;
    public delegate* unmanaged[Cdecl]<byte*, int, void**, int> CreateObject;
    public delegate* unmanaged<
                nint,                               // Length of SCC collection
                StronglyConnectedComponent*,        // SCC collection
                nint,                               // Length of CCR collection
                ComponentCrossReference*,           // CCR collection
                void> MarkCrossReferences;
}

public unsafe sealed class Init
{
    internal static BridgeContext* s_BridgeContext;

    [UnmanagedCallersOnly(
        EntryPoint = "Java_JavaApp_DotnetInit",             // Defined name by JNI. See Java build for generated header file.
        CallConvs = new Type[] { typeof(CallConvCdecl) })   // Cdecl is needed to match JNI signature on x86. Ignored on other platforms.
    ]
    public static void Initialize(nint jniEnv, nint _, void* bridgeContextRaw)
    {
        Console.WriteLine("DnLib!DnLib.Init.Initialize()");

        s_BridgeContext = (BridgeContext*)bridgeContextRaw;

        Debug.Assert(s_BridgeContext->InitializeBridge is not null);
        s_BridgeContext->InitializeBridge();

        Debug.Assert(s_BridgeContext->MarkCrossReferences is not null);
#pragma warning disable CA1416 // Validate platform compatibility
        JavaMarshal.Initialize(s_BridgeContext->MarkCrossReferences);
#pragma warning restore CA1416 // Validate platform compatibility
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
            GC.Collect();
        }

        Console.WriteLine($"Mark collectible nodes");
        {
            //using Marshaller marshaller = new(root.BuildJavaReferenceGraph(c1));
            GC.Collect();
        }

        root.Print("2-");

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
}
