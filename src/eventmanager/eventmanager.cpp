
#include "eventmanager.h"

//CEventManager& CEventManager::Instance()
//{
//    static CEventManager eventMgr;
//    return eventMgr;
//}

CEventManager::CEventManager()
{
#ifdef ENABLE_EVENT_SIGNATURE_VERIFY
    //mapEventSignature.emplace(EID_BLOCK, EventSignature<int, std::string, bool>());
    //mapEventSignature.emplace(EID_TRANSACTION, EventSignature<std::vector<int>, int*>());
    // ...
#endif
    Init();
}

CEventManager::~CEventManager()
{
    Uninit(true);
}

int CEventManager::Init()
{
    EVENT_LOCK_GUARD(mutex)
    if (mapEventDispatchers.empty())
    {
        mapEventDispatchers.emplace(MID_ALL_MODULE, std::move(std::unique_ptr<CEventDispatcher>(new CEventDispatcher)));
        return 0;
    }
    return 1;
}

int CEventManager::Uninit(bool refined)
{
    EVENT_LOCK_GUARD(mutex)
    if (!mapEventDispatchers.empty())
    {
        std::vector<CEventDispatcher*> dispatchers;
        for (auto& dispatcher : mapEventDispatchers)
        {
            dispatchers.push_back(dispatcher.second.get());
        }

        // In order to avoid deadlock, we unlock mutex first before waiting for thread exit.
        {
            EVENT_UNLOCK_GUARD(mutex)

            for (auto& dispatcher : dispatchers)
            {
                dispatcher->Interrupt(!refined);
            }

            for (auto& dispatcher : dispatchers)
            {
                dispatcher->WaitExit();
            }
        }

        mapEventDispatchers.clear();
        mapEventHandlers.clear();
        mapObjModules.clear();
        return 0;
    }
    return 1;
}

#ifndef EVENT_UNLOCK_MODE
int CEventManager::UnregisterEventHandler(int eventID, void* receiver)
{
    int removeCount = 0;
    EVENT_LOCK_GUARD(mutex)
    auto it = mapEventHandlers.find(eventID);
    if (it != mapEventHandlers.end())
    {
        auto & handlers = it->second;
        if (!handlers.empty())
        {
#ifdef EVENT_WEAKLOCK_MODE
            // we should prevent handler from removed while some of handlers is invoking.
            if (handlers.begin()->second.flags & EHF_HANDLER_INVOKING)
            {
                // removeCount = -1;
                return -1; // return value (-1) means unregister failed.
            }
#endif
            auto itHandler = handlers.begin();
            while (itHandler != handlers.end())
            {
                if (itHandler->second.receiver == receiver)
                {
                    removeCount++;
                    itHandler = handlers.erase(itHandler);
                }
                else
                {
                    ++itHandler;
                }
            }
        }
    }
    return removeCount;
}

int CEventManager::UnregisterEventHandler(int eventID)
{
    int removeCount = 0;
    EVENT_LOCK_GUARD(mutex)
    auto it = mapEventHandlers.find(eventID);
    if (it != mapEventHandlers.end())
    {
        auto & handlers = it->second;
        if (!handlers.empty())
        {
#ifdef EVENT_WEAKLOCK_MODE
            // we should prevent handler from removed while some of handlers is invoking.
            if (handlers.begin()->second.flags & EHF_HANDLER_INVOKING)
            {
                // removeCount = -1;
                return -1; // return value (-1) means unregister failed.
            }
#endif
            removeCount = (int)handlers.size();
            mapEventHandlers.erase(it);
        }
    }
    return removeCount;
}

int CEventManager::UnregisterEventHandlerInsistently(int eventID, void* receiver)
{
    int removeCount = 0;
    while (true)
    {
        int ret = UnregisterEventHandler(eventID, receiver);
        if (ret >= 0)
        {
            removeCount += ret;
            break;
        }
        std::this_thread::yield();
    }
    return removeCount;
}
#endif

int CEventManager::SetEventReceiverModule(int module, std::initializer_list<void*> receivers)
{
    if (receivers.size() > 0)
    {
        EVENT_LOCK_GUARD(mutex)
        for (auto receiver : receivers)
        {
            mapObjModules.emplace(receiver, module);
        }
        return 0;
    }
    return -1;
}

int CEventManager::RunEventDispatcherForModules(uint32 modulesMask)
{
    EVENT_LOCK_GUARD(mutex)
    auto it = mapEventDispatchers.find(modulesMask);
    if (it == mapEventDispatchers.end())
    {
        mapEventDispatchers.emplace(modulesMask, std::move(std::unique_ptr<CEventDispatcher>(new CEventDispatcher)));
        return 0;
    }
    return -1;
}

