namespace DnLib;

public interface INode
{
    void AddReference(object obj);
    void ClearReferences();

    // Utility method for prototyping.
    void Print(string prefix);
}