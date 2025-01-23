import java.util.ArrayList;

public class Node implements INode {
    private ArrayList<Object> m_instances = new ArrayList<>();
    private final int m_id;

    public Node() {
        m_id = 0;
    }

    public Node(int id) {
        assert id != 0;
        m_id = id;
    }

    @Override
    public void addReference(Object instance) {
        m_instances.add(instance);
    }

    @Override
    public void clearReferences() {
        m_instances.clear();
    }

    @Override
    public void print(String prefix) {
        if (m_id == 0) {
            System.out.println(prefix + "Root Node");
        } else {
            System.out.println(prefix + "Node " + m_id);
        }

        for (Object instance : m_instances) {
            if (instance instanceof INode) {
                ((INode)instance).print("  " + prefix);
            }
        }
    }

    public void print() {
        System.out.println("JVM object graph:");
        print("|- ");
    }
}
