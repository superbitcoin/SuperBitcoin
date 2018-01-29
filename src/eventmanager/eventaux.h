#ifndef __SBTC_EVENTAUX_H__
#define __SBTC_EVENTAUX_H__

#include <typeinfo>
#include <string>
#include <mutex>
#include <condition_variable>

template<typename Mutex>
class CEventLockGuard
{
public:
    CEventLockGuard (const CEventLockGuard&) = delete;

    explicit CEventLockGuard(Mutex& m) : mutex(m)
    {
        mutex.lock();
    }

    ~CEventLockGuard()
    {
        mutex.unlock();
    }

private:
    Mutex& mutex;
};

template<typename Mutex>
class CEventUnlockGuard
{
public:
    CEventUnlockGuard (const CEventUnlockGuard&) = delete;

    explicit CEventUnlockGuard(Mutex& m) : mutex(m)
    {
        mutex.unlock();
    }

    ~CEventUnlockGuard()
    {
        mutex.lock();
    }

private:
    Mutex& mutex;
};

class CMultiWaiter
{
public:
    explicit CMultiWaiter(int nValueIn) : nValue(nValueIn) {}

    void Wait()
    {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [&]() { return nValue <= 0; });
    }

    void Wake ()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            nValue--;
        }
        cond.notify_one();
    }

private:
    int nValue;
    std::mutex mutex;
    std::condition_variable cond;
};


template <typename... TArgs>
struct EventFoo;

template <typename TFirst, typename... TArgs>
struct EventFoo<TFirst, TArgs...>
{
    static std::string name()
    {
        return std::string(typeid(TFirst).name()) + " " + EventFoo<TArgs...>::name();
    }
};

template <>
struct EventFoo<>
{
    static std::string name()
    {
        return "";
    }
};

template <typename... TArgs>
std::string EventSignature()
{
    return EventFoo<TArgs...>::name();
}

#endif //__SBTC_EVENTAUX_H__
