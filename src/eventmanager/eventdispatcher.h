#ifndef __SBTC_EVENTDISPATCHER_H__
#define __SBTC_EVENTDISPATCHER_H__

#include <deque>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>
#include "eventbasic.h"
#include "eventaux.h"

class CEventDispatcher
{
public:
    CEventDispatcher();

    ~CEventDispatcher();

    int AddAsyncEvent(std::unique_ptr<EventQueuedItem> item);

    void Interrupt(bool rude = false);

    std::thread::id GetThreadID() const;

private:
    void WaitExit();

    void Dispatch();

private:
    bool interrupted;
    bool rudeInterrupted;
    std::thread dispThread;
    std::thread::id dispID;
    std::mutex mutex;
    std::condition_variable cond;
    std::deque<std::unique_ptr<EventQueuedItem>> queue;
};

#endif //__SBTC_EVENTDISPATCHER_H__
