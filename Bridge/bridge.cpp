// Standard headers
#include <cstdio>
#include <cstdlib>

// JVM headers
#include <jni.h>
#include <jvmti.h>

#define BEGIN_EXTERN_C extern "C" {
#define END_EXTERN_C }

namespace
{
    struct BridgeContext
    {
        jvmtiEnv* Jvmti;
        JNIEnv* JNIenv;
        void (JNICALL *Callback)(void*);
    } BridgeContext;

    // Forward declaration
    void JNICALL DotnetCallback(void* cxt);

    void VMInit(
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
    }

    void JNICALL DotnetCallback(void* cxt)
    {
        std::printf("Bridge!DotnetCallback()\n");
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

    // Enable the VMInit() callback.
    jvmtiError err;
    jvmtiEventCallbacks cb{};
    cb.VMInit = &VMInit;
    err = jvmti->SetEventCallbacks(&cb, sizeof(cb));
    if (err != JVMTI_ERROR_NONE)
    {
        std::printf("Failed to SetEventCallbacks(): %d\n", err);
        return JNI_ERR;
    }

    err = jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, (jthread)nullptr);
    if (err != JVMTI_ERROR_NONE)
    {
        std::printf("Failed to SetEventNotificationMode(): %d\n", err);
        return JNI_ERR;
    }

    return JNI_OK;
}

END_EXTERN_C
