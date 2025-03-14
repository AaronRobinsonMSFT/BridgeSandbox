using System.Diagnostics;
using System.Runtime.InteropServices;

namespace DnLib;

public abstract class BaseNode
{
    protected List<object> _references = new List<object>();
}