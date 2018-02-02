#ifndef __SBTC_EVENTMANAGER_H__
#define __SBTC_EVENTMANAGER_H__

#include <map>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <memory>
#include <mutex>
#include <initializer_list>
#include <boost/any.hpp>
#include "eventid.h"
#include "moduleid.h"
#include "eventdispatcher.h"

//#define EVENT_UNLOCK_MODE
//#define EVENT_WEAKLOCK_MODE
//#define EVENT_STRONGLOCK_MODE

// In this mode, all of operation to CEventManager's data will not be protected by unique mutex.
// In other words, Caller should pay more attention to use event manager,
// and ensure that any init/reister/set/startup methods would be finished before
// sendEvent/postEvent/postEventAndWait methods.
//#define EVENT_UNLOCK_MODE

// In this mode, the accessing to CEventManager's data will be protected by unique mutex,
// but mutex would be release during the invoking of event handler, therefore another event
// can be sent in the current event handler at the same thread without deadlock.
#define EVENT_WEAKLOCK_MODE

// In this mode, the whole accessing to CEventManager's data as well as the invoking of event handler
// will be protected by unique mutex.
//#define EVENT_STRONGLOCK_MODE

#if !defined(EVENT_UNLOCK_MODE) && !defined(EVENT_WEAKLOCK_MODE) && !defined(EVENT_STRONGLOCK_MODE)
# error "the above three macro must be defined one at least."
#endif

#ifndef EVENT_UNLOCK_MODE
# define EVENT_LOCK_GUARD(m) std::lock_guard<std::mutex> lck(m);
# define EVENT_UNLOCK_GUARD(m) CEventUnlockGuard<std::mutex> unlck(m);
#else
# define EVENT_LOCK_GUARD(m)
# define EVENT_UNLOCK_GUARD(m)
#endif

//#define ENABLE_EVENT_SIGNATURE_VERIFY

class CEventManager
{
public:
    //static CEventManager& Instance();

//private:

    CEventManager();

    CEventManager(const CEventManager&) = delete;

    CEventManager& operator= (const CEventManager&) = delete;

    ~CEventManager();

    template<typename... TArgs>
    bool MatchEventSignature(int eventID)
    {
        auto it = mapEventSignature.find(eventID);
        return it == mapEventSignature.end() || it->second == EventSignature<TArgs...>();
    }

public:
    int Init();

    int Uninit(bool refined);

    // register event handler with class member func.
    template<typename C, typename R, typename... TArgs>
    int RegisterEventHandler(int eventID, C* receiver, R(C::*memfun)(TArgs...), int prior = EHP_MEDIAN, int flags = EHF_NOTHING)
    {
        if (receiver)
        {
#ifdef ENABLE_EVENT_SIGNATURE_VERIFY
            if (!MatchEventSignature<TArgs...>(eventID))
                return ERC_BADSIGNATURE;
#endif
            EventHandleItem handler;
            handler.flags = flags;
            handler.priority = -prior;
            handler.receiver = receiver;

            std::function<void(TArgs...)> fn = [receiver, memfun](TArgs... args){ (receiver->*memfun)(std::forward<TArgs>(args)...); };
            handler.handler = std::move(fn);

            EVENT_LOCK_GUARD(mutex)
            auto& handlers = mapEventHandlers[eventID];
            handlers.emplace(handler.priority, std::move(handler));
            return 0;
        }
        return -1;
    };

    // register event handler with class const member func.
    template<typename C, typename R, typename... TArgs>
    int RegisterEventHandler(int eventID, C* receiver, R(C::*memfun)(TArgs...) const, int prior = EHP_MEDIAN, int flags = EHF_NOTHING)
    {
        return RegisterEventHandler(eventID, receiver, (R(C::*)(TArgs...))memfun, prior, flags);
    };

    template<typename C, typename R, typename... TArgs>
    int RegisterEventHandler(int eventID, C& receiver, R(C::*memfun)(TArgs...), int prior = EHP_MEDIAN, int flags = EHF_NOTHING)
    {
        return RegisterEventHandler(eventID, &receiver, memfun, prior, flags);
    };

    template<typename C, typename R, typename... TArgs>
    int RegisterEventHandler(int eventID, C& receiver, R(C::*memfun)(TArgs...) const, int prior = EHP_MEDIAN, int flags = EHF_NOTHING)
    {
        return RegisterEventHandler(eventID, &receiver, (R(C::*)(TArgs...))memfun, prior, flags);
    };

    // register event handler with non-member func. or class static member func.
    template<typename R, typename... TArgs>
    int RegisterEventHandler(int eventID, R(*func)(TArgs...), int prior = EHP_MEDIAN, int flags = EHF_NOTHING)
    {
        if (func)
        {
#ifdef ENABLE_EVENT_SIGNATURE_VERIFY
            if (!MatchEventSignature<TArgs...>(eventID))
                return ERC_BADSIGNATURE;
#endif
            EventHandleItem handler;
            handler.flags = flags;
            handler.priority = -prior;
            handler.receiver = nullptr;
            handler.handler = std::function<void(TArgs...)>(func);

            EVENT_LOCK_GUARD(mutex)
            auto& handlers = mapEventHandlers[eventID];
            handlers.emplace(handler.priority, std::move(handler));
            return 0;
        }
        return -1;
    };

    // register event handler with a functor  or function object.
    // notice that caller need to specify template type arguments list because compiler cannot deduce those type arguments.
    template<typename... TArgs, typename TFunctor>
    int RegisterEventHandler(int eventID, const TFunctor& functor, int prior = EHP_MEDIAN, int flags = EHF_NOTHING)
    {
#ifdef ENABLE_EVENT_SIGNATURE_VERIFY
        if (!MatchEventSignature<TArgs...>(eventID))
            return ERC_BADSIGNATURE;
#endif
        EventHandleItem handler;
        handler.flags = flags;
        handler.priority = -prior;
        handler.receiver = nullptr;
        handler.handler = std::function<void(TArgs...)>([functor](TArgs... args){ (const_cast<TFunctor&>(functor))(std::forward<TArgs>(args)...); });

        EVENT_LOCK_GUARD(mutex)
        auto& handlers = mapEventHandlers[eventID];
        handlers.emplace(handler.priority, std::move(handler));
        return 0;
    };

#ifndef EVENT_UNLOCK_MODE
    // this method may faild because of some handler are invoking.
    int UnregisterEventHandler(int eventID, void* receiver);

    // this method may faild because of some handler are invoking.
    int UnregisterEventHandler(int eventID);

    // this method must be successful.
    int UnregisterEventHandlerInsistently(int eventID, void* receiver);
#endif

    int SetEventReceiverModule(int module, std::initializer_list<void*> receivers);

    template<typename... TArgs>
    int SetEventReceiverModule(int module, TArgs... receivers)
    {
        return SetEventReceiverModule(module, {receivers...});
// C++14
//#if __cplusplus >= 201300L
//        if (receivers.size() > 0)
//        {
//            auto op = [&](auto receiver){
//                mapObjModules[receiver] = module;
//            };
//            EVENT_LOCK_GUARD(mutex)
//            int arr[] = {(op(receivers), 0)...};
//            return 0;
//        }
//        return -1;
//#endif
    }

    // Run a new thread to execute those event handlers whose receiver object live in modules specified by modulesMask.
    // Any other posted event handler will be executed in a default dispatcher thread.
    int RunEventDispatcherForModules(uint32 modulesMask);

    template<typename... TArgs>
    int SendEvent(int eventID, TArgs... args)
    {
#ifdef ENABLE_EVENT_SIGNATURE_VERIFY
        if (!MatchEventSignature<TArgs...>(eventID))
            return ERC_BADSIGNATURE;
#endif
        EVENT_LOCK_GUARD(mutex)
        auto it = mapEventHandlers.find(eventID);
        if (it != mapEventHandlers.end())
        {
            auto& handlers = it->second; // References to elements in the unordered_map container remain valid in all cases, even after a rehash.
            if (!handlers.empty())
            {
                handlers.begin()->second.flags |= EHF_HANDLER_INVOKING; // prevent handlers from removed.
                {
#ifdef EVENT_WEAKLOCK_MODE
                    EVENT_UNLOCK_GUARD(mutex)
#endif
                    for (auto& eachHandler : handlers)
                    {
                        std::function<void(TArgs...)> callback = boost::any_cast<std::function<void(TArgs...)>>(eachHandler.second.handler);
                        callback(args...);
                    }
                }
                handlers.begin()->second.flags &= ~EHF_HANDLER_INVOKING;
                return 0;
            }
        }
        return -1;
    }

    template<typename... TArgs>
    int PostEvent(int eventID, TArgs... args)
    {
#ifdef ENABLE_EVENT_SIGNATURE_VERIFY
        if (!MatchEventSignature<TArgs...>(eventID))
            return ERC_BADSIGNATURE;
#endif
        EVENT_LOCK_GUARD(mutex)
        auto it = mapEventHandlers.find(eventID);
        if (it != mapEventHandlers.end())
        {
            std::thread::id currentThreadID = std::this_thread::get_id();
            auto& handlers = it->second;
            for (auto& eachHandler : handlers)
            {
                std::unique_ptr<EventQueuedItem> queueItem(new EventQueuedItem);
                queueItem->flags = eachHandler.second.flags;
                queueItem->postID = currentThreadID;
                queueItem->syncObj = nullptr;

                std::function<void(TArgs...)> callback = boost::any_cast<std::function<void(TArgs...)>>(eachHandler.second.handler);
                std::function<void()> fn = [callback, args...](){ callback(args...); };
                queueItem->handler = std::move(fn);

                uint32 module = (uint32)MID_ALL_MODULE;
                auto itModule = mapObjModules.find(eachHandler.second.receiver);
                if (itModule != mapObjModules.end())
                {
                    module = (uint32)itModule->second;
                }

                auto itDisp = mapEventDispatchers.lower_bound(module);
                while (itDisp != mapEventDispatchers.end() && !(itDisp->first & module))
                {
                    ++itDisp;
                }

                if (itDisp != mapEventDispatchers.end()) // assert
                {
                    itDisp->second->AddAsyncEvent(std::move(queueItem));
                }
            }
            return 0;
        }
        return -1;
    }

    template<typename... TArgs>
    int PostEventAndWait(int eventID, TArgs... args)
    {
#ifdef ENABLE_EVENT_SIGNATURE_VERIFY
        if (!MatchEventSignature<TArgs...>(eventID))
            return ERC_BADSIGNATURE;
#endif
        std::unique_lock<std::mutex> lock(mutex);
        auto it = mapEventHandlers.find(eventID);
        if (it != mapEventHandlers.end())
        {
            auto& handlers = it->second;
            CMultiWaiter multiWaiter((int)handlers.size());
            std::thread::id currentThreadID = std::this_thread::get_id();
            for (auto& eachHandler : handlers)
            {
                uint32 module = (uint32)MID_ALL_MODULE;
                auto itModule = mapObjModules.find(eachHandler.second.receiver);
                if (itModule != mapObjModules.end())
                {
                    module = (uint32)itModule->second;
                }

                auto itDisp = mapEventDispatchers.lower_bound(module);
                while (itDisp != mapEventDispatchers.end() && !(itDisp->first & module))
                {
                    ++itDisp;
                }

                if (itDisp != mapEventDispatchers.end()) // assert
                {
                    if (itDisp->second->GetThreadID() == currentThreadID)
                    {
                        // we can't post an event to the same thread, instead we invoke event handler directly here.
                        std::function<void(TArgs...)> callback = boost::any_cast<std::function<void(TArgs...)>>(eachHandler.second.handler);
                        callback(args...); // Should we release lock here?
                        multiWaiter.Wake();
                    }
                    else
                    {
                        std::unique_ptr<EventQueuedItem> queueItem(new EventQueuedItem);
                        queueItem->flags = eachHandler.second.flags;
                        queueItem->postID = currentThreadID;
                        queueItem->syncObj = &multiWaiter;

                        std::function<void(TArgs...)> callback = boost::any_cast<std::function<void(TArgs...)>>(eachHandler.second.handler);
                        std::function<void()> fn = [callback, args...](){ callback(args...); };
                        queueItem->handler = std::move(fn);

                        itDisp->second->AddAsyncEvent(std::move(queueItem));
                    }
                }
            }

            lock.unlock(); // now we first unlock mutex before waiting for async handler.
            multiWaiter.Wait();
            return 0;
        }
        return -1;
    }

private:
    std::mutex mutex;
    std::unordered_map<void*, int> mapObjModules;
    std::unordered_map<int, std::string> mapEventSignature;
    std::unordered_map<int, std::multimap<int, EventHandleItem>> mapEventHandlers;
    std::map<uint32, std::unique_ptr<CEventDispatcher>> mapEventDispatchers;
};

#endif //__SBTC_EVENTMANAGER_H__
