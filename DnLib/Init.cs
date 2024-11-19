using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace DnLib;

// BridgeContext defined in bridge.cpp.
[StructLayout(LayoutKind.Sequential)]
public unsafe struct BridgeContext
{
    public void* Jvmti;
    public void* JNIEnv;
    public delegate* unmanaged[Cdecl]<void*, void> Callback; // Cdecl is needed for x86 scenarios. Ignored on other platforms.
}

public unsafe sealed class Init
{
    [UnmanagedCallersOnly(
        EntryPoint = "Java_JavaApp_DotnetInit",             // Defined name by JNI. See Java build for generated header file.
        CallConvs = new Type[] { typeof(CallConvCdecl) })   // Cdecl is needed to match JNI signature on x86. Ignored on other platforms.
    ]
    public static void Initialize(nint jniEnv, nint _, void* bridgeContextRaw)
    {
        Console.WriteLine("DnLib!DnLib.Init.Initialize()");

        var bridgeContext = (BridgeContext*)bridgeContextRaw;

        // Use the supplied callback to call back into the Bridge.
        bridgeContext->Callback(bridgeContext);
    }
}
