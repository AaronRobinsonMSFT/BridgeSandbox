using System.Runtime.InteropServices;

namespace DnLib;

[Guid("329b458a-98cf-41b2-80b4-4f3254af50b2")]
public interface IJVMObject
{
    IntPtr GetJNIHandle();
}