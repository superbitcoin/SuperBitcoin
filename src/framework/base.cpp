#include <thread>
#include <chrono>
#include <functional>
#include <signal.h>
#include "base.hpp"
#include "config/argmanager.h"
#include "config/chainparamsbase.h"
#include "config/chainparams.h"

using namespace appbase;

log4cpp::Category &CApp::mlog = log4cpp::Category::getInstance(EMTOSTR(CID_APP));

CApp &CApp::Instance()
{
    static  CApp _app;
    return _app;
}

CApp::CApp()
    : nVersion(1), bShutdown(false), cArgs(new CArgsManager)
{
}

bool CApp::InitParams(int argc, char *argv[])
{

    if (!PreInit() || !cArgs->Init(argc, argv))
    {
        return false;
    }

    try
    {
        if (!fs::is_directory(cArgs->GetDataDir(false)))
        {
            mlog.error("Error: Specified data directory \"%s\" does not exist.",
                    cArgs->GetArg<std::string>("-datadir", "").c_str());
            return false;
        }

        try
        {
            cArgs->ReadConfigFile(cArgs->GetArg<std::string>("-conf", BITCOIN_CONF_FILENAME));
        }
        catch (const std::exception &e)
        {
            mlog.error("Error reading configuration file: %s.", e.what());
            return false;
        }

        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        try
        {
            bool fRegTest = cArgs->IsArgSet("-regtest");
            bool fTestNet = cArgs->IsArgSet("-testnet");
            if (fRegTest && fTestNet)
            {
                mlog.error("Invalid combination of -regtest and -testnet.");
                return false;
            }

            std::string networkType = CBaseChainParams::MAIN;
            if (fRegTest)
            {
                networkType = CBaseChainParams::REGTEST;
            }

            if (fTestNet)
            {
                networkType = CBaseChainParams::TESTNET;
            }

            cBaseChainParams = CreateBaseChainParams(networkType);
            cChainParams = CreateChainParams(networkType);
        }
        catch (const std::exception &e)
        {
            mlog.error("Error: %s.", e.what());
            return false;
        }

        // -server defaults to true for bitcoind but not for the GUI so do this here
        cArgs->SoftSetArg("-server", true);
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "CApp::initParams");
    }
    catch (...)
    {
        PrintExceptionContinue(nullptr, "CApp::initParams");
    }

    return true;
}

bool CApp::Initialize(int argc, char **argv)
{
    return InitParams(argc, argv) && ForEachComponent(true, [](IComponent* component){ return component->Initialize(); });
}

bool CApp::Startup()
{
    return ForEachComponent(true, [](IComponent* component){ return component->Startup(); });
}

bool CApp::Shutdown()
{
    return ForEachComponent<std::function<bool(IComponent*)>, ReverseContainerIterator>(false, [](IComponent* component){ return component->Shutdown(); });
}

bool CApp::Run()
{
    while (!bShutdown)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    return true;
}


bool CApp::RegisterComponent(IComponent* component)
{
    if (component)
    {
        int id = component->GetID();
        if (m_mapComponents.find(id) == m_mapComponents.end())
        {
            m_mapComponents.emplace(id, component);
            return true;
        }
    }

    return false;
}


static void HandleSIGTERM(int)
{
    app().RequestShutdown();
}

bool CApp::PreInit()
{

#ifndef WIN32
    const CArgsManager &appArgs = app().GetArgsManager();
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
    //avoid mutity call
    assert(scheduler == nullptr);
    assert(eventManager == nullptr);
    assert(uiInterface == nullptr);
    scheduler.reset(new CScheduler);
    eventManager.reset(new CEventManager);
    uiInterface.reset(new CClientUIInterface);

    return scheduler && eventManager;
}


CScheduler &CApp::GetScheduler()
{
    return *scheduler.get();
}

CEventManager &CApp::GetEventManager()
{
    return *eventManager.get();
}

CClientUIInterface &CApp::GetUIInterface()
{
    return *uiInterface.get();
}


