#include <iostream>
#include "basecomponent.hpp"
#include "scheduler.h"
#include "ui_interface.h"
#include "eventmanager/eventmanager.h"

CBaseComponent::CBaseComponent()
{

}

CBaseComponent::~CBaseComponent()
{

}

bool CBaseComponent::ComponentInitialize()
{
    std::cout << "initialize base component \n";

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


