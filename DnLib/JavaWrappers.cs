using System.Diagnostics;
using System.Runtime.InteropServices;

namespace DnLib;

internal sealed unsafe class JavaWrappers : ComWrappers
{
    protected override unsafe ComInterfaceEntry* ComputeVtables(object obj, CreateComInterfaceFlags flags, out int count)
    {
        Debug.Assert(obj is JavaNode);
        Debug.Assert(!flags.HasFlag(CreateComInterfaceFlags.CallerDefinedIUnknown));
        Debug.Assert(flags.HasFlag(CreateComInterfaceFlags.TrackerSupport));

        count = 0;
        return null;
    }

    protected override unsafe object CreateObject(nint externalComObject, CreateObjectFlags flags)
        => throw new NotImplementedException();

    protected override unsafe void ReleaseObjects(System.Collections.IEnumerable objects)
        => throw new NotImplementedException();
}