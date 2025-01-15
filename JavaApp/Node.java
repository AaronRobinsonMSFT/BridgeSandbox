import java.util.ArrayList;

public class Node implements INode {
    private ArrayList<Object> instances = new ArrayList<>();

    @Override
    public void addReference(Object instance) {
        instances.add(instance);
    }

    @Override
    public void clearReferences() {
        instances.clear();
    }

    @Override
    public void print(String prefix) {
        System.out.println(prefix + "Node");
        for (Object instance : instances) {
            if (instance instanceof INode) {
                ((INode)instance).print("  " + prefix);
            }
        }
    }

    public void print() {
        print("&- ");
    }
}
