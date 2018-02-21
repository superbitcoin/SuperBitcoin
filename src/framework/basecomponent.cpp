#include <iostream>
#include <csignal>
#include "base.hpp"
#include "basecomponent.hpp"
#include "scheduler.h"
#include "ui_interface.h"
#include "eventmanager/eventmanager.h"
#include "config/argmanager.h"
#ifndef WIN32
# include <sys/stat.h>
#endif

CBaseComponent::CBaseComponent()
{

}

CBaseComponent::~CBaseComponent()
{

}

static void HandleSIGTERM(int)
{
    app().RequestShutdown();
}

bool CBaseComponent::ComponentInitialize()
{
    std::cout << "initialize base component \n";

#ifndef WIN32
    const CArgsManager& appArgs = app().GetArgsManager();
    if (!appArgs.GetArg<bool>("sysperms", false))
        umask(077);

    // Clean shutdown on SIGTERM
    signal(SIGTERM, (sighandler_t)HandleSIGTERM);
    signal(SIGINT, (sighandler_t)HandleSIGTERM);

    // Reopen debug.log on SIGHUP
    // signal(SIGHUP, (sighandler_t)HandleSIGHUP);

    // signal(SIGUSR1, (sighandler_t)reload_handler);

    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);
#endif

    // std::set_new_handler(new_handler_terminate);

    scheduler.reset(new CScheduler);
    eventManager.reset(new CEventManager);
    uiInterface.reset(new CClientUIInterface);

    return scheduler && eventManager;
}

bool CBaseComponent::ComponentStartup()
{
    std::cout << "startup base component \n";

    if (scheduler)
    {
        schedulerThread = std::thread(&CScheduler::serviceQueue, scheduler.get());
        return true;
    }

    return false;
}

bool CBaseComponent::ComponentShutdown()
{
    std::cout << "shutdown base component \n";

    if (scheduler)
    {
        scheduler->stop();
        if (schedulerThread.joinable())
        {
            schedulerThread.join();
        }
    }

    if (eventManager)
    {
        eventManager->Uninit(true);
    }

    return true;
}

const char* CBaseComponent::whoru() const
{
    return "I am CBaseCommonent\n";
}

CScheduler* CBaseComponent::GetScheduler() const
{
    return scheduler.get();
}

CEventManager* CBaseComponent::GetEventManager() const
{
    return eventManager.get();
}

CClientUIInterface* CBaseComponent::GetUIInterface() const
{
    return uiInterface.get();
}


