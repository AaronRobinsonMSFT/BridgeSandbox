using System.Collections;
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
}

public unsafe sealed class Init
{
    private static JavaWrappers s_JavaWrappers = new JavaWrappers();
    private static BridgeContext* s_BridgeContext;

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

        void* instance;
        ReadOnlySpan<byte> className = "java/lang/String"u8;
        fixed (byte* ptr = &ReadOnlySpanMarshaller<byte, byte>.ManagedToUnmanagedIn.GetPinnableReference(className))
        {
            int hr = s_BridgeContext->CreateObject(ptr, null, &instance);
            Marshal.ThrowExceptionForHR(hr);
        }

        IJVMObject jvmObj = (IJVMObject)s_JavaWrappers.GetOrCreateObjectForComInstance((IntPtr)instance, CreateObjectFlags.TrackerObject);
        Marshal.Release((IntPtr)instance);

        Console.WriteLine($"JNI Handle: {jvmObj.GetJNIHandle():X}");

        GC.Collect();
        GC.WaitForPendingFinalizers();
    }
}

[Guid("329b458a-98cf-41b2-80b4-4f3254af50b2")]
public interface IJVMObject
{
    IntPtr GetJNIHandle();
}
