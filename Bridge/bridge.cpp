// Standard headers
#include <cstdio>
#include <cstdlib>
#include <cinttypes>
#include <array>
#include <cassert>

#include "bridge.hpp"

#define BEGIN_EXTERN_C extern "C" {
#define END_EXTERN_C }

namespace
{
    struct BridgeContext
    {
        jvmtiEnv* Jvmti;
        JNIEnv* JNIenv;
        void (JNICALL *Callback)(void*);
        HRESULT (JNICALL *CreateObject)(char const*, int32_t, void**);
        void (JNICALL *SetObjectGraph)(int, void*);
    } BridgeContext;

    // Forward declaration
    void JNICALL DotnetCallback(void* cxt);
    HRESULT JNICALL CreateObject(char const* className, int32_t id, void** instance);

    void JNICALL VMInit(
        jvmtiEnv* jvmti,
        JNIEnv* env,
        jthread)
    {
        std::printf("Bridge!VMInit()\n");

        // Fill out the Bridge context that will be passed
        // to .NET.
        BridgeContext.Jvmti = jvmti;
        BridgeContext.JNIenv = env;
        BridgeContext.Callback = &DotnetCallback;
        BridgeContext.CreateObject = &CreateObject;
        BridgeContext.SetObjectGraph = &SetObjectGraph;

        // Find the class and static field to update.
        char const* className = "JavaApp";
        jclass classID = env->FindClass(className);
        if (classID == nullptr)
        {
            std::printf("Failed to find class, '%s', to set Bridge context\n", className);
            exit(1);
        }

        char const* fieldName = "s_BridgeContext";
        jfieldID fieldID = env->GetStaticFieldID(classID, fieldName, "J");
        if (fieldID == nullptr)
        {
            std::printf("Failed to find static field, '%s', to set Bridge context\n", fieldName);
            exit(1);
        }

        // Set static field value to the address of the BridgeContext.
        // This will be passed to a native export to the .NET environment.
        env->SetStaticLongField(classID, fieldID, (jlong)&BridgeContext);
        env->DeleteLocalRef(classID);

        // Initialize the tracker host with the JVM details.
        InitializeTrackerHost(jvmti, env);
    }

    void JNICALL GCStartCallback(jvmtiEnv*)
    {
        std::printf("JVM Garbage Collection started.\n");
    }

    void JNICALL GCFinishCallback(jvmtiEnv*)
    {
        std::printf("JVM Garbage Collection finished.\n");
    }

    void JNICALL ObjectFreeCallback(jvmtiEnv*, jlong tag)
    {
        std::printf("JVM Object freed: %" PRId64 "\n", (int64_t)tag);
    }

    void JNICALL DotnetCallback(void* cxt)
    {
        std::printf("Bridge!DotnetCallback()\n");
    }

    HRESULT JNICALL CreateObject(char const* className, int32_t id, void** instance)
    {
        jclass klass = BridgeContext.JNIenv->FindClass(className);
        jmethodID constructor = BridgeContext.JNIenv->GetMethodID(klass, "<init>", "(I)V");
        jobject obj = BridgeContext.JNIenv->NewObject(klass, constructor, id);

        jlong tag = (jlong)id;
        jvmtiError error = BridgeContext.Jvmti->SetTag(obj, tag);
        assert(error == JVMTI_ERROR_NONE);
        (void)error;

        jobject objRef = BridgeContext.JNIenv->NewGlobalRef(obj);
        HRESULT hr = CreateTrackerInstance(objRef, (IUnknown**)instance);
        if (FAILED(hr))
            BridgeContext.JNIenv->DeleteGlobalRef(objRef);

        BridgeContext.JNIenv->DeleteLocalRef(obj);
        BridgeContext.JNIenv->DeleteLocalRef(klass);
        return hr;
    }
}

BEGIN_EXTERN_C

JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM* vm, char* options, void* reserved)
{
    jvmtiEnv* jvmti = nullptr;
    if (0 != vm->GetEnv((void**)&jvmti, JVMTI_VERSION)) // Use the most recent version possible
    {
        std::printf("Failed to GetEnv()\n");
        return JNI_ERR;
    }

    jvmtiError err;
    jvmtiCapabilities capabilities{};
    capabilities.can_generate_garbage_collection_events = 1;
    capabilities.can_generate_object_free_events = 1;
    capabilities.can_tag_objects = 1;
    err = jvmti->AddCapabilities(&capabilities);
    if (err != JVMTI_ERROR_NONE)
    {
        std::printf("Failed to add capabilities: %d\n", err);
        return JNI_ERR;
    }

    // Enable callbacks.
    jvmtiEventCallbacks cb{};
    cb.VMInit = &VMInit;
    cb.GarbageCollectionStart = &GCStartCallback;
    cb.GarbageCollectionFinish = &GCFinishCallback;
    cb.ObjectFree = &ObjectFreeCallback;
    err = jvmti->SetEventCallbacks(&cb, sizeof(cb));
    if (err != JVMTI_ERROR_NONE)
    {
        std::printf("Failed to SetEventCallbacks(): %d\n", err);
        return JNI_ERR;
    }

    std::array events = {
        JVMTI_EVENT_VM_INIT,
        JVMTI_EVENT_GARBAGE_COLLECTION_START,
        JVMTI_EVENT_GARBAGE_COLLECTION_FINISH,
        JVMTI_EVENT_OBJECT_FREE
    };
    for (jvmtiEvent event : events)
    {
        err = jvmti->SetEventNotificationMode(JVMTI_ENABLE, event, (jthread)nullptr);
        if (err != JVMTI_ERROR_NONE)
        {
            std::printf("Failed to SetEventNotificationMode(%d): %d\n", event, err);
            return JNI_ERR;
        }
    }

    return JNI_OK;
}

END_EXTERN_C
