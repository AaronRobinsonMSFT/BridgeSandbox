public interface INode {
    void addReference(Object instance);
    void clearReferences();

    // Utility method for prototyping.
    void print(String prefix);
}