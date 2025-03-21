// Standard headers
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <thread>
#include <mutex>
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

        // Threading related members
        std::thread _workerThread;
        std::mutex _mutex;
        std::condition_variable _cv;
        bool _taskReady;
        bool _shouldStop;

        // Task data
        size_t _sccsLen;
        StronglyConnectedComponent* _sccs;
        size_t _ccrsLen;
        ComponentCrossReference* _ccrs;

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
            , _nodeClearReferences{ nullptr }
            , _taskReady{ false }
            , _shouldStop{ false }
            , _sccsLen{ 0 }
            , _sccs{ nullptr }
            , _ccrsLen{ 0 }
            , _ccrs{ nullptr }
        {
        }

        ~TrackerRuntimeManagerImpl()
        {
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _shouldStop = true;
                _taskReady = true;
            }
            _cv.notify_one();

            if (_workerThread.joinable())
                _workerThread.join();
        }

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

            // Start GC Bridge worker thread
            _workerThread = std::thread(&TrackerRuntimeManagerImpl::WorkerThreadFunc, this);
        }

    private:
        void WorkerThreadFunc()
        {
            JavaVM *jvm;
            (void)_jnienv->GetJavaVM(&jvm);

            JNIEnv* env = nullptr;
            jint res = jvm->AttachCurrentThreadAsDaemon((void**)&env, nullptr);
            if (res != JNI_OK)
            {
                std::printf("Failed to attach current thread to JVM\n");
                exit(-1);
            }

            while (true)
            {
                std::unique_lock<std::mutex> lock(_mutex);
                _cv.wait(lock, [this] { return _taskReady; });

                if (_shouldStop)
                    break;

                // Process the task
                ProcessMarkCrossReferencesTask();

                // Reset the task flag
                _taskReady = false;
            }
        }

        void ProcessMarkCrossReferencesTask()
        {
            // Reify the object graph in the JVM
            StronglyConnectedComponent* sccs_curr = _sccs;
            StronglyConnectedComponent* sccs_end = _sccs + _sccsLen;
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

            ComponentCrossReference* ccrs_curr = _ccrs;
            ComponentCrossReference* ccrs_end = _ccrs + _ccrsLen;
            for (; ccrs_curr != ccrs_end; ++ccrs_curr)
            {
                if (ccrs_curr->SourceGroupIndex == ccrs_curr->DestinationGroupIndex)
                    continue;

                jobject* src = _sccs[ccrs_curr->SourceGroupIndex].ContextMemory[0];
                jobject* dst = _sccs[ccrs_curr->DestinationGroupIndex].ContextMemory[0];
                JVMAddReference(src[0], dst[0]);
            }

            // Convert all JVM references to weak references
            sccs_curr = _sccs; // Reset the iterator
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
            sccs_curr = _sccs; // Reset the iterator
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

            int32_t* removeList = nullptr;
            if (removeCount > 0)
            {
                removeList = (int32_t*)std::malloc(sizeof(int32_t) * removeCount);
                int32_t* remove_curr = removeList;

                sccs_curr = _sccs; // Reset the iterator
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
            }

            _removeRefsCallback(removeCount, removeList, _sccsLen, _sccs, _ccrsLen, _ccrs);
            std::free(removeList);
        }

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

            // Mutex is already held, so return immediately
            if (!_mutex.try_lock())
                return;

            //
            // [NOTE] If the GCHandles need to be "locked", the bridge should do this here.
            // The current prototype doesn't implement this feature.
            //

            assert(!_taskReady);
            _sccsLen = sccsLen;
            _sccs = sccs;
            _ccrsLen = ccrsLen;
            _ccrs = ccrs;
            _taskReady = true;
            _mutex.unlock();  // Release the lock before triggering the condition variable

            // Notify the worker thread
            _cv.notify_one();
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
