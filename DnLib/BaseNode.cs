using System.Diagnostics;
using System.Runtime.InteropServices;

namespace DnLib;

[StructLayout(LayoutKind.Sequential)]
public struct JavaReferences
{
    public IntPtr Object;
    public object ObjectManagedLifetime;
    public IntPtr[] References;
    public bool Collectible;

    public override string ToString()
    {
        return $"Object: {Object:X}, ObjectManagedLifetime: {ObjectManagedLifetime}, References: {string.Join(", ", References.Select(r => r.ToString("X")))}, Collectible: {Collectible}";
    }
}

public abstract class BaseNode
{
    protected List<object> _references = new List<object>();

    protected virtual IntPtr Handle { get; }

    public bool Collectible { get; private set; } = false;

    public JavaReferences[] BuildJavaReferenceGraph(params INode[] collectibleNodes)
    {
        // Check if the object is collectible.
        Collectible = collectibleNodes.Any(c => ReferenceEquals(c, this));

        List<IntPtr> directs = new();
        List<JavaReferences> references = new();
        foreach (object inst in _references)
        {
            JavaReferences[] refs;
            if (inst is JavaNode javaNode)
            {
                directs.Add(javaNode.Handle);
                refs = javaNode.BuildJavaReferenceGraph(collectibleNodes);
            }
            else if (inst is DotnetNode dnNode)
            {
                refs = dnNode.BuildJavaReferenceGraph(collectibleNodes);
            }
            else
            {
                throw new InvalidOperationException("Unknown node type.");
            }

            Debug.Assert(refs.Length >= 1);
            ref JavaReferences direct = ref refs[0];
            if (direct.Object != IntPtr.Zero)
            {
                references.AddRange(refs);
            }
            else
            {
                // Fold the direct references into the current node, if the removed node isn't collectible.
                if (!direct.Collectible)
                {
                    directs.AddRange(direct.References);
                }
                else
                {
                    // Since the references are being dropped, we need to Release them.
                    foreach (IntPtr r in direct.References)
                    {
                        Marshal.Release(r);
                    }
                }

                // Propagate the collectible state to the references.
                foreach (JavaReferences r in refs.Skip(1))
                {
                    references.Add(r with { Collectible = direct.Collectible });
                }
            }
        }

        return references.Prepend(new JavaReferences { Object = Handle, ObjectManagedLifetime = this, References = directs.ToArray(), Collectible = Collectible }).ToArray();
    }

    public void PruneReferences()
    {
        foreach (object inst in _references)
        {
            if (inst is BaseNode node)
            {
                node.PruneReferences();
            }
        }

        _references.RemoveAll(r => r is BaseNode node && node.Collectible);
    }
}