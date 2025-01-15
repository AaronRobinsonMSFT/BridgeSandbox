public interface INode {
    void addReference(Object instance);
    void clearReferences();
    void print(String prefix);
}