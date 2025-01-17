// Standard headers
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <atomic>
#include <cassert>
#include <new>

#include "bridge.hpp"

namespace API
{
    // Documentation found at https://learn.microsoft.com/windows/win32/api/windows.ui.xaml.hosting.referencetracker/
    //64bd43f8-bfee-4ec4-b7eb-2935158dae21
    const GUID IID_IReferenceTrackerTarget = { 0x64bd43f8, 0xbfee, 0x4ec4, { 0xb7, 0xeb, 0x29, 0x35, 0x15, 0x8d, 0xae, 0x21} };
    class DECLSPEC_UUID("64bd43f8-bfee-4ec4-b7eb-2935158dae21") IReferenceTrackerTarget : public IUnknown
    {
    public:
        STDMETHOD_(ULONG, AddRefFromReferenceTracker)() = 0;
        STDMETHOD_(ULONG, ReleaseFromReferenceTracker)() = 0;
        STDMETHOD(Peg)() = 0;
        STDMETHOD(Unpeg)() = 0;
    };

    //29a71c6a-3c42-4416-a39d-e2825a07a773
    const GUID IID_IReferenceTrackerHost = { 0x29a71c6a, 0x3c42, 0x4416, { 0xa3, 0x9d, 0xe2, 0x82, 0x5a, 0x07, 0xa7, 0x73} };
    class DECLSPEC_UUID("29a71c6a-3c42-4416-a39d-e2825a07a773") IReferenceTrackerHost : public IUnknown
    {
    public:
        STDMETHOD(DisconnectUnusedReferenceSources)(_In_ DWORD dwFlags) = 0;
        STDMETHOD(ReleaseDisconnectedReferenceSources)() = 0;
        STDMETHOD(NotifyEndOfReferenceTrackingOnThread)() = 0;
        STDMETHOD(GetTrackerTarget)(_In_ IUnknown* obj, _Outptr_ IReferenceTrackerTarget** ppNewReference) = 0;
        STDMETHOD(AddMemoryPressure)(_In_ uint64_t bytesAllocated) = 0;
        STDMETHOD(RemoveMemoryPressure)(_In_ uint64_t bytesAllocated) = 0;
    };

    //3cf184b4-7ccb-4dda-8455-7e6ce99a3298
    const GUID IID_IReferenceTrackerManager = { 0x3cf184b4, 0x7ccb, 0x4dda, { 0x84, 0x55, 0x7e, 0x6c, 0xe9, 0x9a, 0x32, 0x98} };
    class DECLSPEC_UUID("3cf184b4-7ccb-4dda-8455-7e6ce99a3298") IReferenceTrackerManager : public IUnknown
    {
    public:
        STDMETHOD(ReferenceTrackingStarted)() = 0;
        STDMETHOD(FindTrackerTargetsCompleted)(_In_ BOOL bWalkFailed) = 0;
        STDMETHOD(ReferenceTrackingCompleted)() = 0;
        STDMETHOD(SetReferenceTrackerHost)(_In_ IReferenceTrackerHost *pCLRServices) = 0;
    };

    class DECLSPEC_UUID("04b3486c-4687-4229-8d14-505ab584dd88") IFindReferenceTargetsCallback : public IUnknown
    {
    public:
        STDMETHOD(FoundTrackerTarget)(_In_ IReferenceTrackerTarget* target) = 0;
    };

    //11d3b13a-180e-4789-a8be-7712882893e6
    const GUID IID_IReferenceTracker = { 0x11d3b13a, 0x180e, 0x4789, { 0xa8, 0xbe, 0x77, 0x12, 0x88, 0x28, 0x93, 0xe6} };
    class DECLSPEC_UUID("11d3b13a-180e-4789-a8be-7712882893e6") IReferenceTracker : public IUnknown
    {
    public:
        STDMETHOD(ConnectFromTrackerSource)() = 0;
        STDMETHOD(DisconnectFromTrackerSource)() = 0;
        STDMETHOD(FindTrackerTargets)(_In_ IFindReferenceTargetsCallback *pCallback) = 0;
        STDMETHOD(GetReferenceTrackerManager)(_Outptr_ IReferenceTrackerManager **ppTrackerManager) = 0;
        STDMETHOD(AddRefFromTrackerSource)() = 0;
        STDMETHOD(ReleaseFromTrackerSource)() = 0;
        STDMETHOD(PegFromTrackerSource)() = 0;
    };

    const GUID IID_IJVMObject = { 0x329b458a, 0x98cf, 0x41b2, {0x80, 0xb4, 0x4f, 0x32, 0x54, 0xaf, 0x50, 0xb2} };
    class DECLSPEC_UUID("329b458a-98cf-41b2-80b4-4f3254af50b2") IJVMObject : public IUnknown
    {
    public:
        STDMETHOD(GetJNIHandle)(_Out_ intptr_t* target) = 0;
    };
}

namespace
{
    class TrackerObject final : public IUnknown
    {
        class TrackerObjectImpl final : public API::IJVMObject, public API::IReferenceTracker
        {
            IUnknown* _implOuter;
            std::atomic<int> _trackerSourceCount;
            jobject _instance;

        public:
            TrackerObjectImpl(_In_ jobject instance, _In_ IUnknown* pUnkOuter)
                : _implOuter{ pUnkOuter }
                , _trackerSourceCount{ 0 }
                , _instance{ instance }
            {
                assert(_instance != nullptr);
            }

            ~TrackerObjectImpl() = default;

            void SetJNIHandle(intptr_t handle)
            {
                _instance = reinterpret_cast<jobject>(handle);
            }

        public: // IJVMObject
            STDMETHOD(GetJNIHandle)(_Out_ intptr_t* target)
            {
                assert(target != nullptr);
                *target = reinterpret_cast<intptr_t>(_instance);
                return S_OK;
            }

        public: // IReferenceTracker
            STDMETHOD(ConnectFromTrackerSource)();
            STDMETHOD(DisconnectFromTrackerSource)();
            STDMETHOD(FindTrackerTargets)(_In_ API::IFindReferenceTargetsCallback* pCallback);
            STDMETHOD(GetReferenceTrackerManager)(_Outptr_ API::IReferenceTrackerManager** ppTrackerManager);
            STDMETHOD(AddRefFromTrackerSource)();
            STDMETHOD(ReleaseFromTrackerSource)();
            STDMETHOD(PegFromTrackerSource)();

        public: // IUnknown
            STDMETHOD(QueryInterface)(
                /* [in] */ REFIID riid,
                /* [iid_is][out] */ void ** ppvObject)
            {
                return _implOuter->QueryInterface(riid, ppvObject);
            }
            STDMETHOD_(ULONG, AddRef)(void)
            {
                return _implOuter->AddRef();
            }
            STDMETHOD_(ULONG, Release)(void)
            {
                return _implOuter->Release();
            }
        };

        std::atomic<uint32_t> _refCount;
        IUnknown* _outer;
        TrackerObjectImpl _impl;

    public:
        TrackerObject(_In_ jobject instance, _In_opt_ IUnknown* pUnkOuter)
            : _refCount{ 1 }
            , _outer{ pUnkOuter == nullptr ? static_cast<IUnknown*>(this) : pUnkOuter }
            , _impl{ instance, _outer }
        {
        }

        ~TrackerObject() = default;

        jobject GetJNIHandle()
        {
            intptr_t handle = 0;
            (void)_impl.GetJNIHandle(&handle);
            return reinterpret_cast<jobject>(handle);
        }

        void SetJNIHandle(jobject handle)
        {
            _impl.SetJNIHandle(reinterpret_cast<intptr_t>(handle));
        }

    public: // IUnknown
        STDMETHOD(QueryInterface)(
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void ** ppvObject)
        {
            if (ppvObject == nullptr)
                return E_POINTER;

            IUnknown* tgt;

            // Aggregation implementation.
            if (riid == IID_IUnknown)
            {
                tgt = static_cast<IUnknown*>(this);
            }
            else
            {
                // Send non-IUnknown queries to the implementation.
                if (riid == API::IID_IReferenceTracker)
                {
                    tgt = static_cast<API::IReferenceTracker*>(&_impl);
                }
                else if (riid == API::IID_IJVMObject)
                {
                    tgt = static_cast<API::IJVMObject*>(&_impl);
                }
                else
                {
                    *ppvObject = nullptr;
                    return E_NOINTERFACE;
                }
            }

            (void)tgt->AddRef();
            *ppvObject = tgt;
            return S_OK;
        }

        STDMETHOD_(ULONG, AddRef)(void)
        {
            uint32_t count = ++_refCount;
            return (ULONG)count;
        }

        STDMETHOD_(ULONG, Release)(void)
        {
            uint32_t count = --_refCount;
            if (count == 0)
                delete this;
            return (uint32_t)count;
        }
    };

    class TrackerRuntimeManagerImpl final : public API::IReferenceTrackerManager
    {
        API::IReferenceTrackerHost* _runtimeServices;
        jvmtiEnv* _jvmti;
        JNIEnv* _jnienv;

        jclass _systemClass;
        jmethodID _gcMethod;

        jobject _rootNode;
        jmethodID _nodePrint;
        jmethodID _nodeAddReference;
        jmethodID _nodeClearReferences;

        int _objectGraphLength;
        void* _objectGraphTmp;

    public:
        TrackerRuntimeManagerImpl()
            : _runtimeServices{ nullptr }
            , _jvmti{ nullptr }
            , _jnienv{ nullptr }
            , _systemClass{ nullptr }
            , _gcMethod{ nullptr }
            , _rootNode{ nullptr }
            , _nodePrint{ nullptr }
            , _nodeAddReference{ nullptr }
            , _objectGraphLength{ 0 }
            , _objectGraphTmp{ nullptr }
        { }

        ~TrackerRuntimeManagerImpl() = default;

        void SetJVMState(jvmtiEnv* jvmti, JNIEnv* env)
        {
            assert(_jvmti == nullptr);
            assert(_jnienv == nullptr);
            _jvmti = jvmti;
            _jnienv = env;

            jclass systemClass = _jnienv->FindClass("java/lang/System");
            _systemClass = static_cast<jclass>(_jnienv->NewGlobalRef(systemClass));
            _gcMethod = _jnienv->GetStaticMethodID(_systemClass, "gc", "()V");
            _jnienv->DeleteLocalRef(systemClass);
        }

        void JVMInitializeObjectGraph()
        {
            assert(_jnienv != nullptr);

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

        void SetObjectGraph(int length, void* graph)
        {
            _objectGraphLength = length;
            _objectGraphTmp = graph;
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
            assert(target != nullptr && reference != nullptr);

            _jnienv->CallVoidMethod(target, _nodeAddReference, reference);
        }

        void JVMConvertToWeakReference(TrackerObject* obj)
        {
            assert(_jnienv != nullptr);
            assert(obj != nullptr);

            jobject ref = obj->GetJNIHandle();
            jobject weakRef = _jnienv->NewWeakGlobalRef(ref);
            obj->SetJNIHandle(weakRef);
            _jnienv->DeleteGlobalRef(ref);
        }

        bool JVMConvertToStrongReference(TrackerObject* obj)
        {
            assert(_jnienv != nullptr);
            assert(obj != nullptr);

            jobject weakRef = obj->GetJNIHandle();
            jobject ref = _jnienv->NewGlobalRef(weakRef);
            obj->SetJNIHandle(ref);
            _jnienv->DeleteWeakGlobalRef(weakRef);

            return ref != nullptr;
        }

    public: // IReferenceTrackerManager
        STDMETHOD(ReferenceTrackingStarted)()
        {
            if (_objectGraphTmp == nullptr) // Need to check if graph has been set.
                return S_OK;

            std::printf("TrackerRuntimeManagerImpl::ReferenceTrackingStarted()\n");

            // See JavaReferencesUnmanaged in Init.cs
            struct JavaReferences
            {
                TrackerObject* Object;
                TrackerObject** References;
                uint8_t Collectible;
            };

            // Reify the object graph in the JVM
            JavaReferences* objGraph = (JavaReferences*)_objectGraphTmp;
            for (int i = 0; i < _objectGraphLength; ++i)
            {
                JavaReferences& r = objGraph[i];

                // Skip collectible objects
                if (r.Collectible)
                    continue;

                // Set the root node
                if (i == 0)
                    JVMAddReference(_rootNode, r.Object->GetJNIHandle());

                for (int j = 0; r.References[j] != nullptr; ++j)
                    JVMAddReference(r.Object->GetJNIHandle(), r.References[j]->GetJNIHandle());
            }

            JVMPrintNode();

            // Convert all references to weak references
            for (int i = 0; i < _objectGraphLength; ++i)
            {
                JavaReferences& r = objGraph[i];
                JVMConvertToWeakReference(r.Object);
            }

            JVMTriggerGC();

            // Convert all references back to strong references and clear references in Java.
            for (int i = 0; i < _objectGraphLength; ++i)
            {
                JavaReferences& r = objGraph[i];
                if (JVMConvertToStrongReference(r.Object))
                {
                    _jnienv->CallVoidMethod(r.Object->GetJNIHandle(), _nodeClearReferences);
                }
            }

            // Clear the references in the root node
            _jnienv->CallVoidMethod(_rootNode, _nodeClearReferences);

            return S_OK;
        }

        STDMETHOD(FindTrackerTargetsCompleted)(_In_ BOOL bWalkFailed)
        {
            // Nothing to walk in the Java heap.
            return S_OK;
        }

        STDMETHOD(ReferenceTrackingCompleted)()
        {
            if (_objectGraphTmp == nullptr) // Need to check if graph has been set.
                return S_OK;

            std::printf("TrackerRuntimeManagerImpl::ReferenceTrackingCompleted()\n");

            // Reset object graph state
            _objectGraphTmp = nullptr;
            _objectGraphLength = 0;
            return S_OK;
        }

        STDMETHOD(SetReferenceTrackerHost)(_In_ API::IReferenceTrackerHost* pHostServices)
        {
            assert(pHostServices != nullptr);
            return pHostServices->QueryInterface(API::IID_IReferenceTrackerHost, (void**)&_runtimeServices);
        }

        // Lifetime maintained by stack - we don't care about ref counts
        STDMETHOD_(ULONG, AddRef)() { return 1; }
        STDMETHOD_(ULONG, Release)() { return 1; }

        STDMETHOD(QueryInterface)(
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void ** ppvObject)
        {
            if (ppvObject == nullptr)
                return E_POINTER;

            if (IsEqualIID(riid, API::IID_IReferenceTrackerManager))
            {
                *ppvObject = static_cast<API::IReferenceTrackerManager*>(this);
            }
            else if (IsEqualIID(riid, IID_IUnknown))
            {
                *ppvObject = static_cast<IUnknown*>(this);
            }
            else
            {
                *ppvObject = nullptr;
                return E_NOINTERFACE;
            }

            (void)AddRef();
            return S_OK;
        }
    };

    TrackerRuntimeManagerImpl TrackerRuntimeManager;

    HRESULT STDMETHODCALLTYPE TrackerObject::TrackerObjectImpl::ConnectFromTrackerSource()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TrackerObject::TrackerObjectImpl::DisconnectFromTrackerSource()
    {
        std::printf("TrackerObject being finalized in .NET: %p\n", this);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TrackerObject::TrackerObjectImpl::FindTrackerTargets(_In_ API::IFindReferenceTargetsCallback* pCallback)
    {
        // Nothing to walk in the Java heap.
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TrackerObject::TrackerObjectImpl::GetReferenceTrackerManager(_Outptr_ API::IReferenceTrackerManager** ppTrackerManager)
    {
        // Initialize objects needed for managing the JVM object graph.
        TrackerRuntimeManager.JVMInitializeObjectGraph();

        return TrackerRuntimeManager.QueryInterface(API::IID_IReferenceTrackerManager, (void**)ppTrackerManager);
    }

    HRESULT STDMETHODCALLTYPE TrackerObject::TrackerObjectImpl::AddRefFromTrackerSource()
    {
        assert(0 <= _trackerSourceCount);
        ++_trackerSourceCount;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TrackerObject::TrackerObjectImpl::ReleaseFromTrackerSource()
    {
        assert(0 < _trackerSourceCount);
        --_trackerSourceCount;

        if (_trackerSourceCount == 0)
            std::printf("TrackerObject released from .NET\n");

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TrackerObject::TrackerObjectImpl::PegFromTrackerSource()
    {
        /* Not used by runtime */
        return E_NOTIMPL;
    }
}

void JNICALL SetObjectGraph(int length, void* graph)
{
    TrackerRuntimeManager.SetObjectGraph(length, graph);
}

void InitializeTrackerHost(jvmtiEnv* jvmti, JNIEnv* env)
{
    TrackerRuntimeManager.SetJVMState(jvmti, env);
}

HRESULT CreateTrackerInstance(jobject obj, IUnknown* outer, IUnknown** tracker)
{
    assert(obj != nullptr);
    try
    {
        TrackerObject* pTracker = new TrackerObject(obj, outer);
        HRESULT hr = pTracker->QueryInterface(IID_IUnknown, (void**)tracker);
        pTracker->Release();
        return hr;
    }
    catch (std::bad_alloc const&)
    {
        return E_OUTOFMEMORY;
    }
}