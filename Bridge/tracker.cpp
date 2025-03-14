// Standard headers
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <cassert>

#include "bridge.hpp"

namespace
{
    class TrackerRuntimeManagerImpl final
    {
        jvmtiEnv* _jvmti;
        JNIEnv* _jnienv;

        RemoveReferencesCallback _removeRefsCallback;

        bool _attachedToThread;

        jclass _systemClass;
        jmethodID _gcMethod;

        jobject _rootNode;
        jmethodID _nodePrint;
        jmethodID _nodeAddReference;
        jmethodID _nodeClearReferences;

    public:
        TrackerRuntimeManagerImpl()
            : _jvmti{ nullptr }
            , _jnienv{ nullptr }
            , _removeRefsCallback{ nullptr }
            , _attachedToThread{ false }
            , _systemClass{ nullptr }
            , _gcMethod{ nullptr }
            , _rootNode{ nullptr }
            , _nodePrint{ nullptr }
            , _nodeAddReference{ nullptr }
        { }

        ~TrackerRuntimeManagerImpl() = default;

        void SetJVMState(jvmtiEnv* jvmti, JNIEnv* env)
        {
            assert(_jvmti == nullptr);
            assert(_jnienv == nullptr);
            _jvmti = jvmti;
            _jnienv = env;

            // Acquire the handles to trigger a GC
            // https://docs.oracle.com/en/java/javase/20/docs/api/java.base/java/lang/System.html#gc()
            jclass systemClass = _jnienv->FindClass("java/lang/System");
            _systemClass = static_cast<jclass>(_jnienv->NewGlobalRef(systemClass));
            _gcMethod = _jnienv->GetStaticMethodID(_systemClass, "gc", "()V");
            _jnienv->DeleteLocalRef(systemClass);
        }

        void JVMInitializeObjectGraph(RemoveReferencesCallback callback)
        {
            assert(_jnienv != nullptr);

            _removeRefsCallback = callback;
            assert(_removeRefsCallback != nullptr);

            jclass javaAppClass = _jnienv->FindClass("JavaApp");
            assert(javaAppClass != nullptr);

            jfieldID fieldID = _jnienv->GetStaticFieldID(javaAppClass, "s_Node", "LNode;");
            assert(fieldID != nullptr);

            // Get and store the value of the static field
            jobject nodeInst = _jnienv->GetStaticObjectField(javaAppClass, fieldID);
            assert(nodeInst != nullptr);

            // Store the global reference to the root node
            assert(_rootNode == nullptr);
            _rootNode = _jnienv->NewGlobalRef(nodeInst);

            jclass nodeClass = _jnienv->FindClass("Node");
            assert(nodeClass != nullptr);

            _nodePrint = _jnienv->GetMethodID(nodeClass, "print", "()V");
            assert(_nodePrint != nullptr);

            _nodeAddReference = _jnienv->GetMethodID(nodeClass, "addReference", "(Ljava/lang/Object;)V");
            assert(_nodeAddReference != nullptr);

            _nodeClearReferences = _jnienv->GetMethodID(nodeClass, "clearReferences", "()V");
            assert(_nodeClearReferences != nullptr);

            // Clean up local references
            _jnienv->DeleteLocalRef(nodeClass);
            _jnienv->DeleteLocalRef(nodeInst);
            _jnienv->DeleteLocalRef(javaAppClass);
        }

    private:
        void JVMTriggerGC()
        {
            assert(_jnienv != nullptr);
            assert(_systemClass != nullptr && _gcMethod != nullptr);

            _jnienv->CallStaticVoidMethod(_systemClass, _gcMethod);
        }

        void JVMPrintNode()
        {
            assert(_jnienv != nullptr);
            assert(_rootNode != nullptr && _nodePrint != nullptr);

            _jnienv->CallVoidMethod(_rootNode, _nodePrint);
        }

        void JVMAddReference(jobject target, jobject reference)
        {
            assert(_jnienv != nullptr);
            assert(_nodeAddReference != nullptr);
            if (target == nullptr || reference == nullptr)
                return;

            //std::printf("JVMAddReference(%p, %p)\n", target, reference);
            _jnienv->CallVoidMethod(target, _nodeAddReference, reference);
        }

        void JVMClearReferences(jobject target)
        {
            assert(_jnienv != nullptr);
            assert(_nodeClearReferences != nullptr);
            if (target == nullptr)
                return;

            _jnienv->CallVoidMethod(target, _nodeClearReferences);
        }

        void JVMConvertToWeakReference(jobject* objPtr)
        {
            assert(_jnienv != nullptr);
            assert(objPtr != nullptr);
            if (*objPtr == nullptr)
                return;

            jobject* tgt = objPtr;
            jobject ref = *tgt;
            *tgt = _jnienv->NewWeakGlobalRef(ref);
            _jnienv->DeleteGlobalRef(ref);
        }

        bool JVMConvertToStrongReference(jobject* objPtr)
        {
            assert(_jnienv != nullptr);
            assert(objPtr != nullptr);

            jobject* tgt = objPtr;
            jobject weakRef = *tgt;
            *tgt = _jnienv->NewGlobalRef(weakRef);
            _jnienv->DeleteWeakGlobalRef(weakRef);

            // Converting a weak to strong handle can result in
            // null, which indicates the object was collected.
            return *tgt != nullptr;
        }

    public:
        void MarkCrossReferences(
            size_t sccsLen,
            StronglyConnectedComponent* sccs,
            size_t ccrsLen,
            ComponentCrossReference* ccrs)
        {
            //std::printf("TrackerRuntimeManagerImpl::MarkCrossReferences()\n");

            if (!_attachedToThread)
            {
                JavaVM *jvm;
                (void)_jnienv->GetJavaVM(&jvm);

                JNIEnv* env = nullptr;
                jint res = jvm->AttachCurrentThread((void**)&env, nullptr);
                if (res != JNI_OK)
                {
                    std::printf("Failed to attach current thread to JVM\n");
                    exit(-1);
                }

                // Attach the current thread to the JVM if not already attached
                _attachedToThread = true;
            }

            // Reify the object graph in the JVM
            StronglyConnectedComponent* sccs_curr = sccs;
            StronglyConnectedComponent* sccs_end = sccs + sccsLen;
            for (; sccs_curr != sccs_end; ++sccs_curr)
            {
                if (sccs_curr->Count == 0)
                    continue;
                assert(sccs_curr->Count >= 2);

                jobject* first = sccs_curr->ContextMemory[0];
                jobject* last = first;
                for (size_t i = 1; i < sccs_curr->Count; ++i)
                {
                    jobject* curr = sccs_curr->ContextMemory[i];
                    JVMAddReference(*last, curr[0]);
                    last = curr;
                }
                JVMAddReference(*last, *first);
            }

            ComponentCrossReference* ccrs_curr = ccrs;
            ComponentCrossReference* ccrs_end = ccrs + ccrsLen;
            for (; ccrs_curr != ccrs_end; ++ccrs_curr)
            {
                if (ccrs_curr->SourceGroupIndex == ccrs_curr->DestinationGroupIndex)
                    continue;

                jobject* src = sccs[ccrs_curr->SourceGroupIndex].ContextMemory[0];
                jobject* dst = sccs[ccrs_curr->DestinationGroupIndex].ContextMemory[0];
                JVMAddReference(src[0], dst[0]);
            }

            // Convert all JVM references to weak references
            sccs_curr = sccs; // Reset the iterator
            for (; sccs_curr != sccs_end; ++sccs_curr)
            {
                for (size_t i = 0; i < sccs_curr->Count; ++i)
                {
                    jobject* curr = sccs_curr->ContextMemory[i];
                    JVMConvertToWeakReference(curr);
                }
            }

            //JVMPrintNode();

            JVMTriggerGC();

            int32_t removeCount = 0;
            // Convert all references back to strong references and clear references in Java.
            sccs_curr = sccs; // Reset the iterator
            for (; sccs_curr != sccs_end; ++sccs_curr)
            {
                for (size_t i = 0; i < sccs_curr->Count; ++i)
                {
                    jobject* curr = sccs_curr->ContextMemory[i];
                    if (!JVMConvertToStrongReference(curr))
                    {
                        removeCount++;
                        continue;
                    }

                    JVMClearReferences(curr[0]);
                }
            }

            if (removeCount > 0)
            {
                int32_t* removeList = (int32_t*)std::malloc(sizeof(int32_t) * removeCount);
                int32_t* remove_curr = removeList;

                sccs_curr = sccs; // Reset the iterator
                for (; sccs_curr != sccs_end; ++sccs_curr)
                {
                    for (size_t i = 0; i < sccs_curr->Count; ++i)
                    {
                        jobject* curr = sccs_curr->ContextMemory[i];
                        if (curr[0] == nullptr)
                        {
                            jobject id = curr[1]; // Get the unique ID of the object
                            *remove_curr = static_cast<int32_t>(reinterpret_cast<intptr_t>(id));
                            ++remove_curr;
                        }
                    }
                }
                _removeRefsCallback(removeCount, removeList);
                std::free(removeList);
            }
        }
    };

    TrackerRuntimeManagerImpl TrackerRuntimeManager;
}

void InitializeTrackerHost(jvmtiEnv* jvmti, JNIEnv* env, RemoveReferencesCallback callback)
{
    TrackerRuntimeManager.SetJVMState(jvmti, env);

    // Initialize objects needed for managing the JVM object graph.
    TrackerRuntimeManager.JVMInitializeObjectGraph(callback);
}

void MarkCrossReferences(
    size_t sccsLen,
    StronglyConnectedComponent* sccs,
    size_t ccrsLen,
    ComponentCrossReference* ccrs)
{
    TrackerRuntimeManager.MarkCrossReferences(sccsLen, sccs, ccrsLen, ccrs);
}
