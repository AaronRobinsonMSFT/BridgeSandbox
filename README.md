# .NET and Java Bridge

The is a sandbox environment to investigate how a Bridge between the two runtimes (that is, CoreCLR and JVM) could be designed.

## Design

A JVM agent is written using the [JVM Tool Interface (JVMTI)](https://docs.oracle.com/en/java/javase/20/docs/specs/jvmti.html). The agent is loaded during JVM application start up and initialized via the `Agent_OnLoad` export. Bridge initialization is done in the following order:

1) On application start-up, the JVM loads the agent and prior to VM initialization calls `Agent_OnLoad`.
2) The agent subscribes to the `VMInit` callback.
3) The JVM is initialized and the `VMInit` callback is called. During the callback the `BridgeContext` is populated and its memory address is set in a `static` field, `s_BridgeContext`, on the application's entry class.
4) The Java application's `main()` is entered.
5) The Java application calls an exported function from a DNNE assembly, passing the value in `s_BridgeContext`.
6) Calling the exported function forces CoreCLR to be activated via DNNE.
7) The exported function calls into the .NET class library, passing in a pointer to the the `BridgeContext` context.
8) The .NET function calls the `Callback` field on the `BridgeContext`, and enters the Bridge.

After the above sequence of events, the JVM, Bridge and .NET library are fully initialized and control of the current thread leaves the Bridge, .NET library and eventually returns from the Java applications `main()` function - see "Run" section below.

The `BridgeContext` context contains pointers to [`jvmtiEnv`](https://docs.oracle.com/en/java/javase/20/docs/specs/jvmti.html) and [`JNIEnv`](https://docs.oracle.com/en/java/javase/20/docs/specs/jni/index.html) instances and a callback into the Bridge that can be expanded as needed. This approach allows for experimentation with GC and type system coordination.

## Requirements

- CMake >= 3.0 - https://cmake.org/download/
- .NET >= 9.0 - https://dotnet.microsoft.com/download
- C++ compiler >= C++ 17 (that is, MSVC, Clang, GCC)
- JDK >= 20 - https://www.oracle.com/java/technologies/downloads/

## Set up

- Download a JDK an set the `JDKPath` environment variable to the root (contains the `bin/` folder).
- Add the JDK's `bin/` directory to the path.
- Confirm that `cmake`, `dotnet`, `javac` and `java` are on the path.

**macOS**: It may be required to define the `DOTNET_ROOT` environment variable if the .NET install is non-standard.

## Build

To build:

`dotnet build build.proj`

To clean:

`dotnet clean build.proj`

The `-c` flag is respected and supports both `Debug` and `Release` builds.

## Run

Navigate to the output directory.

`cd artifacts`

`cd [Release/Debug]`

Run the `JavaApp` application and pass the agent flag to load the Bridge.

`java -agentlib:bridge JavaApp`

Expected output:

```console
$ java -agentlib:bridge JavaApp
Bridge!VMInit()
JavaApp.main()
DnLib!DnLib.Init.Initialize()
Bridge!DotnetCallback()
```
