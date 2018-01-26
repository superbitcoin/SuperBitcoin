#ifndef __SBTC_EVENTBASIC_H__
#define __SBTC_EVENTBASIC_H__

#include <cstdint>
#include <functional>
#include <thread>
#include <boost/any.hpp>

typedef uint32_t uint32;

enum EventHandleFlags
{
    EHF_NOTHING     = 0,
    EHF_SINGLE_THREAD = 1,
    EHF_HANDLER_INVOKING = 2,
};

enum EventHandlePriority
{
    EHP_LOWEST      = 0,
    EHP_LOW         = 1,
    EHP_MEDIAN      = 2,
    EHP_HIGH        = 3,
    EHP_HIGHEST     = 4,
};

struct EventHandleItem
{
    int         flags;      // see EventHandleFlags
    int         priority;   // see EventHandlePriority
    void*       receiver;   // receiver object
    boost::any  handler;    // callback
};

class CMultiWaiter;
struct EventQueuedItem
{
    int             flags;   // see EventHandleFlags
    std::thread::id postID;  // event sender thread id
    CMultiWaiter*   syncObj; // just valid for blocked post
    std::function<void()> handler; // callback.
};

#endif //__SBTC_EVENTBASIC_H__
