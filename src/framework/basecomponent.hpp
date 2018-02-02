#pragma once

#include <memory>
#include <thread>
#include "interface/ibasecomponent.h"

class CBaseComponent : public IBaseComponent
{
public:
    CBaseComponent();

    ~CBaseComponent();

    bool ComponentInitialize() override;

    bool ComponentStartup() override;

    bool ComponentShutdown() override;

    const char *whoru() const override;


    CScheduler* GetScheduler() const override;

    CEventManager* GetEventManager() const override;

    CClientUIInterface* GetUIInterface() const override;


private:
    std::thread schedulerThread;
    std::unique_ptr<CScheduler>    scheduler;
    std::unique_ptr<CEventManager> eventManager;
    std::unique_ptr<CClientUIInterface> uiInterface;
};
