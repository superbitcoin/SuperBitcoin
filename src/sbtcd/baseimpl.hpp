#pragma once
#include <map>
#include <memory>
#include <thread>
#include "base/base.hpp"
#include "utils/iterator.h"
#include "ui_interface.h"
#include "scheduler.h"
#include "eventmanager/eventmanager.h"

using namespace appbase;

class ECCVerifyHandle;

class CApp : public IBaseApp
{
public:

    bool Initialize(int argc, char **argv) override;

    bool Startup() override;

    bool Run() override;

    bool Shutdown() override;


    CScheduler &GetScheduler() override;

    CEventManager &GetEventManager() override;

    CClientUIInterface &GetUIInterface() override;

    bool RegisterComponent(IComponent *component);


private:
    IComponent *FindComponent(int id) const override;

    void InitOptionMap() override;

    void InitParameterInteraction();

    bool AppInitBasicSetup();

    bool AppInitParameterInteraction();

    bool AppInitSanityChecks();

    bool AppInitLockDataDirectory();


    template<typename F, template<typename C> class CI = ContainerIterator>
    bool ForEachComponent(bool breakIfFailed, F &&func)
    {
        bool isOk = true;
        auto it = MakeContainerIterator<CI>(m_mapComponents);
        for (; !it.IsEnd(); it.Next())
        {
            if (IComponent *component = it->second.get())
            {
                if (!func(component))
                {
                    isOk = false;
                    if (breakIfFailed)
                    {
                        break;
                    }
                }
            }
        }
        return isOk;
    }

private:
    std::thread schedulerThread;
    std::unique_ptr<CScheduler> scheduler;
    std::unique_ptr<CEventManager> eventManager;
    std::unique_ptr<CClientUIInterface> uiInterface;
    std::unique_ptr<ECCVerifyHandle> globalVerifyHandle;
    std::map<int, std::unique_ptr<IComponent>> m_mapComponents; ///< all registered plugins ordered by id.
};
