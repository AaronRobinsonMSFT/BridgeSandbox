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
        void (JNICALL *InitializeBridge)(RemoveReferencesCallback);
        HRESULT (JNICALL *CreateObject)(char const*, int32_t, void**, int32_t*);
        decltype(&::MarkCrossReferences) MarkCrossReferences;
    } BridgeContext;

    // Used to acquire determine object identity.
    jclass g_SystemClass;
    jmethodID g_IdentityHashCodeMethod;

    // Forward declaration
    void JNICALL DotnetCallback(void* cxt);
    void JNICALL InitializeBridge(RemoveReferencesCallback callback);
    HRESULT JNICALL CreateObject(char const* className, int32_t id, void** instance, int32_t* instanceId);

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
        BridgeContext.InitializeBridge = &InitializeBridge;
        BridgeContext.CreateObject = &CreateObject;
        BridgeContext.MarkCrossReferences = &MarkCrossReferences;

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

        // Get the handles so we can call System.identityHashCode() method.
        jclass systemClass = env->FindClass("java/lang/System");
        if (systemClass == nullptr)
        {
            std::printf("Failed to find System class\n");
            exit(1);
        }

        g_SystemClass = (jclass)env->NewGlobalRef(systemClass);

        g_IdentityHashCodeMethod = env->GetStaticMethodID(g_SystemClass, "identityHashCode", "(Ljava/lang/Object;)I");
        if (g_IdentityHashCodeMethod == nullptr)
        {
            std::printf("Failed to find identityHashCode method\n");
            exit(1);
        }
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

    void JNICALL InitializeBridge(RemoveReferencesCallback callback)
    {
        std::printf("Bridge!InitializeBridge()\n");

        // Initialize the tracker host with the JVM details.
        InitializeTrackerHost(BridgeContext.Jvmti, BridgeContext.JNIenv, callback);
    }

    HRESULT JNICALL CreateObject(char const* className, int32_t id, void** instance, int32_t* instanceId)
    {
        jclass klass = BridgeContext.JNIenv->FindClass(className);
        jmethodID constructor = BridgeContext.JNIenv->GetMethodID(klass, "<init>", "(I)V");
        jobject obj = BridgeContext.JNIenv->NewObject(klass, constructor, id);

        jlong tag = (jlong)id;
        jvmtiError error = BridgeContext.Jvmti->SetTag(obj, tag);
        assert(error == JVMTI_ERROR_NONE);
        (void)error;

        *instance = (void*)BridgeContext.JNIenv->NewGlobalRef(obj);
        *instanceId = BridgeContext.JNIenv->CallStaticIntMethod(g_SystemClass, g_IdentityHashCodeMethod, obj);

        BridgeContext.JNIenv->DeleteLocalRef(obj);
        BridgeContext.JNIenv->DeleteLocalRef(klass);
        return S_OK;
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
