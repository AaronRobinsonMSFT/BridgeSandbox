// JVM headers
#include <jni.h>
#include <jvmti.h>

#ifdef WINDOWS
#define NOMINMAX
#include <Windows.h>
#endif // WINDOWS

using RemoveReferencesCallback = void (*)(int32_t count, int32_t* ids);

void InitializeTrackerHost(jvmtiEnv* jvmti, JNIEnv* env, RemoveReferencesCallback callback);

// Types and function callback defined by the .NET environment.
// See System.Runtime.InteropServices.Java namespace for details.
struct StronglyConnectedComponent final
{
    size_t Count;
    jobject** ContextMemory; // Memory allocated in the .NET environment.
};

struct ComponentCrossReference final
{
    size_t SourceGroupIndex;
    size_t DestinationGroupIndex;
};

void MarkCrossReferences(
    size_t sccsLen,
    StronglyConnectedComponent* sccs,
    size_t ccrsLen,
    ComponentCrossReference* ccrs);
