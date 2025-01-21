using System.Runtime.InteropServices;

namespace DnLib;

internal sealed class JavaWrappers : ComWrappers
{
    protected override unsafe ComInterfaceEntry* ComputeVtables(object obj, CreateComInterfaceFlags flags, out int count)
        => throw new NotImplementedException();

    protected override unsafe object CreateObject(nint externalComObject, CreateObjectFlags flags)
        => throw new NotImplementedException();

    protected override unsafe void ReleaseObjects(System.Collections.IEnumerable objects)
        => throw new NotImplementedException();
}