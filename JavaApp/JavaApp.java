public class JavaApp {
    // Static field that will updated by the Bridge on startup.
    private static long s_BridgeContext;

    static {
        // Load the .NET activation library.
        System.loadLibrary("DnLibEntry");
    }

    private static native void DotnetInit(long bridgeContext);

    public static void main(String[] args) {
        System.out.println("JavaApp.main()");

        // Initialize the .NET environment.
        DotnetInit(s_BridgeContext);
    }
}