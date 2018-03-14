#pragma once

#include <map>
#include <memory>
#include <thread>
#include "base/base.hpp"
#include "icomponent.h"
#include "utils/iterator.h"
#include "ui_interface.h"
#include "scheduler.h"
#include "interface/componentid.h"
#include "eventmanager/eventmanager.h"
#include "config/argmanager.h"

class CArgsManager;

class CChainParams;

class CChainParams;

class ECCVerifyHandle;

namespace appbase
{
    class CApp : public IBaseApp
    {
    public:
        CApp();

        bool Shutdown() override;

        CScheduler &GetScheduler() override
        {
            return *scheduler.get();
        }

        CEventManager &GetEventManager() override
        {
            return *eventManager.get();
        }

        CClientUIInterface &GetUIInterface() override
        {
            return *uiInterface.get();
        }

    protected:
        bool AppInitialize() override;

        void InitOptionMap() override;

    private:

        CApp(const CApp &) = delete;

        CApp &operator=(const CApp &) = delete;

        void InitParameterInteraction();

        bool AppInitBasicSetup();

        bool AppInitParameterInteraction();

        bool AppInitSanityChecks();

        bool AppInitLockDataDirectory();

    private:
        std::thread schedulerThread;
        std::unique_ptr<CScheduler> scheduler;
        std::unique_ptr<CEventManager> eventManager;
        std::unique_ptr<CClientUIInterface> uiInterface;
        std::unique_ptr<ECCVerifyHandle> globalVerifyHandle;

    };
}
