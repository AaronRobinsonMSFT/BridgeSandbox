using System.Threading;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Java;

namespace DnLib;

internal class HandleMap
{
    public static HandleMap Instance { get; } = new HandleMap();

    private Lock _lock = new ();
    private Dictionary<int, GCHandle> _instances = new();

    private HandleMap() { }

    public void Add(int key, GCHandle obj)
    {
        lock (_lock)
        {
            _instances.Add(key, obj);
        }
    }

    public object? GetObject(int key)
    {
        lock (_lock)
        {
            if (_instances.TryGetValue(key, out GCHandle value))
            {
                return value.Target;
            }
        }

        return null;
    }

    public void Remove(Span<int> toRemove)
    {
        lock (_lock)
        {
            foreach (int id in toRemove)
            {
                if (_instances.Remove(id, out GCHandle value))
                {
#pragma warning disable CA1416 // Validate platform compatibility
                    IntPtr context = JavaMarshal.GetContext(value);
#pragma warning restore CA1416 // Validate platform compatibility
                    JavaNode.ReleaseContext(context);

                    value.Free();
                }
            }
        }
    }

    [UnmanagedCallersOnly]
    internal static unsafe void RemoveUnreachableObjects(int count, int* unreachableIds, nint sccsLen, StronglyConnectedComponent* sccs, nint ccrsLen, ComponentCrossReference* ccrs)
    {
        Console.WriteLine($"Removing {count} unreachable objects.");

        Span<int> ids = new Span<int>(unreachableIds, count);
        Instance.Remove(ids);

        Console.WriteLine($"Freeing CrossReference resources.");

#pragma warning disable CA1416 // Validate platform compatibility
        JavaMarshal.ReleaseMarkCrossReferenceResources(
            new Span<StronglyConnectedComponent>(sccs, (int)sccsLen),
            new Span<ComponentCrossReference>(ccrs, (int)ccrsLen));
#pragma warning restore CA1416 // Validate platform compatibility
    }
}