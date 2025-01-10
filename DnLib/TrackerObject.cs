using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;

namespace DnLib;

internal sealed class JavaWrappers : ComWrappers
{
    protected override unsafe ComInterfaceEntry* ComputeVtables(object obj, CreateComInterfaceFlags flags, out int count)
    {
        count = 0;
        return null;
    }

    protected override unsafe object CreateObject(nint externalComObject, CreateObjectFlags flags)
    {
        int hr = Marshal.QueryInterface(externalComObject, typeof(IJVMObject).GUID, out nint objInst);
        if (hr == 0)
        {
            return new JVMObjectWrapper(objInst);
        }

        throw new NotSupportedException("Only supports IJVMObject");
    }

    protected override unsafe void ReleaseObjects(System.Collections.IEnumerable objects) => throw new NotImplementedException();
}

internal unsafe class JVMObjectWrapper : IJVMObject
{
    private ComWrappersHelper.ClassNative _classNative;

    public JVMObjectWrapper(IntPtr instancePtr)
    {
        _classNative.Instance = instancePtr;
        _classNative.Release = ComWrappersHelper.ReleaseFlags.Instance;
    }

    protected unsafe JVMObjectWrapper(ComWrappers cw, bool aggregateRefTracker)
    {
        ComWrappersHelper.Init<JVMObjectWrapper>(ref _classNative, this, aggregateRefTracker, cw, &CreateInstance);

        static IntPtr CreateInstance(IntPtr outer, out IntPtr inner)
        {
            throw new NotImplementedException();
        }
    }

    ~JVMObjectWrapper()
    {
        ComWrappersHelper.Cleanup(ref _classNative);
    }

    public IntPtr GetJNIHandle()
    {
        nint handle;
        var fptr = ((delegate* unmanaged[MemberFunction]<IntPtr, nint*, int> )(*(*(void***)_classNative.Instance + 3)));
        int hr = fptr(_classNative.Instance, &handle);
        if (hr != 0)
        {
            throw new COMException($"{nameof(GetJNIHandle)}", hr);
        }
        GC.KeepAlive(this);
        return handle;
    }
}

internal class ComWrappersHelper
{
    public static readonly Guid IID_IReferenceTracker = new Guid("11d3b13a-180e-4789-a8be-7712882893e6");

    [Flags]
    public enum ReleaseFlags
    {
        None = 0,
        Instance = 1,
        Inner = 2,
        ReferenceTracker = 4
    }

    public struct ClassNative
    {
        public ReleaseFlags Release;
        public IntPtr Instance;
        public IntPtr Inner;
        public IntPtr ReferenceTracker;
    }

    public unsafe static void Init<T>(
        ref ClassNative classNative,
        object thisInstance,
        bool aggregateRefTracker,
        ComWrappers cw,
        delegate*<IntPtr, out IntPtr, IntPtr> CreateInstance)
    {
        bool isAggregation = typeof(T) != thisInstance.GetType();

        {
            IntPtr outer = default;
            if (isAggregation)
            {
                // Create a managed object wrapper (i.e. CCW) to act as the outer.
                // Passing the CreateComInterfaceFlags.TrackerSupport can be done if
                // IReferenceTracker support is possible.
                //
                // The outer is now owned in this context.
                outer = cw.GetOrCreateComInterfaceForObject(thisInstance, CreateComInterfaceFlags.TrackerSupport);
            }

            // Create an instance of the COM/WinRT type.
            // This is typically accomplished through a call to CoCreateInstance() or RoActivateInstance().
            //
            // Ownership of the outer has been transferred to the new instance.
            // Some APIs do return a non-null inner even with a null outer. This
            // means ownership may now be owned in this context in either aggregation state.
            classNative.Instance = CreateInstance(outer, out classNative.Inner);
        }

        // TEST: Indicate if we should attempt aggregation with ReferenceTracker.
        if (aggregateRefTracker)
        {
            // Determine if the instance supports IReferenceTracker (e.g. WinUI).
            // Acquiring this interface is useful for:
            //   1) Providing an indication of what value to pass during RCW creation.
            //   2) Informing the Reference Tracker runtime during non-aggregation
            //      scenarios about new references.
            //
            // If aggregation, query the inner since that will have the implementation
            // otherwise the new instance will be used. Since the inner was composed
            // it should answer immediately without going through the outer. Either way
            // the reference count will go to the new instance.
            IntPtr queryForTracker = isAggregation ? classNative.Inner : classNative.Instance;
            int hr = Marshal.QueryInterface(queryForTracker, IID_IReferenceTracker, out classNative.ReferenceTracker);
            if (hr != 0)
            {
                classNative.ReferenceTracker = default;
            }
        }

        {
            // Determine flags needed for native object wrapper (i.e. RCW) creation.
            var createObjectFlags = CreateObjectFlags.None;
            IntPtr instanceToWrap = classNative.Instance;

            // Update flags if the native instance is being used in an aggregation scenario.
            if (isAggregation)
            {
                // Indicate the scenario is aggregation
                createObjectFlags |= CreateObjectFlags.Aggregation;

                // The instance supports IReferenceTracker.
                if (classNative.ReferenceTracker != default(IntPtr))
                {
                    createObjectFlags |= CreateObjectFlags.TrackerObject;

                    // IReferenceTracker is not needed in aggregation scenarios.
                    // It is not needed because all QueryInterface() calls on an
                    // object are followed by an immediately release of the returned
                    // pointer - see below for details.
                    Marshal.Release(classNative.ReferenceTracker);

                    // .NET 5 limitation
                    //
                    // For aggregated scenarios involving IReferenceTracker
                    // the API handles object cleanup. In .NET 5 the API
                    // didn't expose an option to handle this so we pass the inner
                    // in order to handle its lifetime.
                    //
                    // The API doesn't handle inner lifetime in any other scenario
                    // in the .NET 5 timeframe.
                    instanceToWrap = classNative.Inner;
                }
            }

            // Create a native object wrapper (i.e. RCW).
            //
            // Note this function will call QueryInterface() on the supplied instance,
            // therefore it is important that the enclosing CCW forwards to its inner
            // if aggregation is involved. This is typically accomplished through an
            // implementation of ICustomQueryInterface.
            cw.GetOrRegisterObjectForComInstance(instanceToWrap, createObjectFlags, thisInstance);
        }

        if (isAggregation)
        {
            // We release the instance here, but continue to use it since
            // ownership was transferred to the API and it will guarantee
            // the appropriate lifetime.
            Marshal.Release(classNative.Instance);
        }
        else
        {
            // In non-aggregation scenarios where an inner exists and
            // reference tracker is involved, we release the inner.
            //
            // .NET 5 limitation - see logic above.
            if (classNative.Inner != default(IntPtr) && classNative.ReferenceTracker != default(IntPtr))
            {
                Marshal.Release(classNative.Inner);
            }
        }

        // The following describes the valid local values to consider and details
        // on their usage during the object's lifetime.
        classNative.Release = ReleaseFlags.None;
        if (isAggregation)
        {
            // Aggregation scenarios should avoid calling AddRef() on the
            // newInstance value. This is due to the semantics of COM Aggregation
            // and the fact that calling an AddRef() on the instance will increment
            // the CCW which in turn will ensure it cannot be cleaned up. Calling
            // AddRef() on the instance when passed to unmanaged code is correct
            // since unmanaged code is required to call Release() at some point.
            if (classNative.ReferenceTracker == default(IntPtr))
            {
                // COM scenario
                // The pointer to dispatch on for the instance.
                // ** Never release.
                classNative.Release |= ReleaseFlags.None; // Instance

                // A pointer to the inner that should be queried for
                //    additional interfaces. Immediately after a QueryInterface()
                //    a Release() should be called on the returned pointer but the
                //    pointer can be retained and used.
                // ** Release in this class's Finalizer.
                classNative.Release |= ReleaseFlags.Inner; // Inner
            }
            else
            {
                // WinUI scenario
                // The pointer to dispatch on for the instance.
                // ** Never release.
                classNative.Release |= ReleaseFlags.None; // Instance

                // A pointer to the inner that should be queried for
                //    additional interfaces. Immediately after a QueryInterface()
                //    a Release() should be called on the returned pointer but the
                //    pointer can be retained and used.
                // ** Never release.
                classNative.Release |= ReleaseFlags.None; // Inner

                // No longer needed.
                // ** Never release.
                classNative.Release |= ReleaseFlags.None; // ReferenceTracker
            }
        }
        else
        {
            if (classNative.ReferenceTracker == default(IntPtr))
            {
                // COM scenario
                // The pointer to dispatch on for the instance.
                // ** Release in this class's Finalizer.
                classNative.Release |= ReleaseFlags.Instance; // Instance
            }
            else
            {
                // WinUI scenario
                // The pointer to dispatch on for the instance.
                // ** Release in this class's Finalizer.
                classNative.Release |= ReleaseFlags.Instance; // Instance

                // This instance should be used to tell the
                //    Reference Tracker runtime whenever an AddRef()/Release()
                //    is performed on newInstance.
                // ** Release in this class's Finalizer.
                classNative.Release |= ReleaseFlags.ReferenceTracker; // ReferenceTracker
            }
        }
    }

    public static void Cleanup(ref ClassNative classNative)
    {
        if (classNative.Release.HasFlag(ReleaseFlags.Inner))
        {
            Marshal.Release(classNative.Inner);
        }
        if (classNative.Release.HasFlag(ReleaseFlags.Instance))
        {
            Marshal.Release(classNative.Instance);
        }
        if (classNative.Release.HasFlag(ReleaseFlags.ReferenceTracker))
        {
            Marshal.Release(classNative.ReferenceTracker);
        }
    }
}
