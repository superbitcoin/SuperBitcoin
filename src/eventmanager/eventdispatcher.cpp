
#include "eventdispatcher.h"


CEventDispatcher::CEventDispatcher() : interrupted(false), rudeInterrupted(false)
{
    dispThread = std::thread(&CEventDispatcher::Dispatch, this);
    dispID = dispThread.get_id();
}

CEventDispatcher::~CEventDispatcher()
{
    WaitExit();
}

std::thread::id CEventDispatcher::GetThreadID() const
{
    return dispID;
}

int CEventDispatcher::AddAsyncEvent(std::unique_ptr<EventQueuedItem> item)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (!interrupted)
    {
        queue.emplace_back(std::move(item));
        if (queue.size() == 1)
        {
            cond.notify_one();
        }
        return 0;
    }
    return -1;
}

void CEventDispatcher::Interrupt(bool rude)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (!interrupted)
    {
        interrupted = true;
        rudeInterrupted = rude;
        cond.notify_all();
    }
}

void CEventDispatcher::WaitExit()
{
    if (dispThread.joinable())
    {
        dispThread.join();
    }
}

void CEventDispatcher::Dispatch()
{
    while (true)
    {
        std::unique_ptr<EventQueuedItem> item;
        {
            std::unique_lock<std::mutex> lock(mutex);
            while (!interrupted && queue.empty())
            {
                cond.wait(lock);
            }

            if (interrupted)
            {
                if (rudeInterrupted)
                {
                    break;
                }
                if (queue.empty())
                {
                    break;
                }
            }

            item = std::move(queue.front());
            queue.pop_front();
        }

        if (!(item->flags & EHF_SINGLE_THREAD))
        {
            item->handler();
            if (item->syncObj)
            {
                item->syncObj->Wake();
            }
        }
        else
        {
            std::thread th([](std::unique_ptr<EventQueuedItem> item)
            {
                item->handler();
                if (item->syncObj)
                {
                    item->syncObj->Wake();
                }
            }, std::move(item));
            th.detach();
        }
    }
}

