// JVM headers
#include <jni.h>
#include <jvmti.h>

#ifdef WINDOWS
#include <Windows.h>
#endif // WINDOWS

void JNICALL SetObjectGraph(int length, void* graph);

void InitializeTrackerHost(jvmtiEnv* jvmti, JNIEnv* env);

HRESULT CreateTrackerInstance(jobject obj, IUnknown** tracker);