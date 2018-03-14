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

static const char DEFAULT_RPCCONNECT[] = "127.0.0.1";
static const bool DEFAULT_NAMED = false;
static const int DEFAULT_HTTP_CLIENT_TIMEOUT = 900;

class CApp : public appbase::IBaseApp
{
public:
    CApp();

    bool Run(int argc, char *argv[]);

    virtual CScheduler &GetScheduler()
    {
        assert(false);
        static CScheduler scheduler;
        return scheduler;
    }

    virtual CEventManager &GetEventManager()
    {
        assert(false);
        static CEventManager eventManager;
        return eventManager;
    }

    virtual CClientUIInterface &GetUIInterface()
    {
        assert(false);
        static CClientUIInterface clientUIInterface;
        return clientUIInterface;
    }

    bool Startup() override;

protected:
    bool AppInitialize() override;

    void InitOptionMap() override;
};