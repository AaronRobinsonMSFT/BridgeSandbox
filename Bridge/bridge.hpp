// JVM headers
#include <jni.h>
#include <jvmti.h>

#ifdef WINDOWS
#include <Windows.h>
#endif // WINDOWS

void InitializeTrackerHost(jvmtiEnv* jvmti, JNIEnv* env);

HRESULT CreateTrackerInstance(jobject obj, IUnknown* outer, IUnknown** tracker);