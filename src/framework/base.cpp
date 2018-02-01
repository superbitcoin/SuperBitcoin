//#include <application.hpp>
#include "base.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/thread/once.hpp>

#include "config/argmanager.h"
#include "config/chainparamsbase.h"
#include "config/chainparams.h"

//#include <iostream>
//#include <fstream>


using namespace appbase;

CBase &app()
{
    return CBase::Instance();
}

CBase &CBase::Instance()
{
    static CBase _app;
    return _app;
}

CBase::CBase()
        : cArgs(new CArgsManager), nVersion(1)
{
}

CBase::~CBase()
{
}

uint64_t CBase::Version() const
{
    return nVersion;
}

void CBase::SetVersion(uint64_t version)
{
    nVersion = version;
}

bool CBase::initParams(int argc, char *argv[])
{
    if (!cArgs->Init(argc, argv))
    {
        return false;
    }

    try
    {
        if (!fs::is_directory(cArgs->GetDataDir(false)))
        {
            fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n",
                    cArgs->GetArg<std::string>("-datadir", "").c_str());
            return false;
        }

        try
        {
            cArgs->ReadConfigFile(cArgs->GetArg<std::string>("-conf", BITCOIN_CONF_FILENAME));
        }
        catch (const std::exception &e)
        {
            fprintf(stderr, "Error reading configuration file: %s\n", e.what());
            return false;
        }

        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        try
        {
            bool fRegTest = cArgs->IsArgSet("-regtest");
            bool fTestNet = cArgs->IsArgSet("-testnet");
            if (fRegTest && fTestNet)
            {
                fprintf(stderr, "Invalid combination of -regtest and -testnet.\n");
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
            fprintf(stderr, "Error: %s\n", e.what());
            return false;
        }

        // -server defaults to true for bitcoind but not for the GUI so do this here
        cArgs->SoftSetArg("-server", true);
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "CBase::initParams");
    }
    catch (...)
    {
        PrintExceptionContinue(nullptr, "CBase::initParams");
    }

    return true;
}

static boost::once_flag onceFlag = BOOST_ONCE_INIT;

bool CBase::InitializeImpl(int argc, char **argv, vector<CBaseComponent*> autostart_components)
{
    boost::call_once(onceFlag, &CBase::initParams, this, argc, argv);

#ifdef ENABLE_COMPONENT_ARG
    if (cArgs->IsArgSet("component"))
    {
        auto vComponent = cArgs->GetArgs("component");
        for (auto &arg : vComponent)
        {
            vector<string> names;
            boost::split(names, arg, boost::is_any_of(" \t,"));
            for (const std::string &name : names)
            {
                CBaseComponent* comp = FindComponent(name);
                if (comp)
                {
                    autostart_components.push_back(comp);
                }
            }
        }
    }
#endif

    for (auto component : autostart_components)
    {
        if (component && component->Initialize())
        {
            m_vecInitializedComponents.push_back(component);
        }
    }

    return true;
}

void CBase::Startup()
{
    for (auto component : m_vecInitializedComponents)
    {
        if (component->Startup())
        {
            m_vecRunningComponents.push_back(component);
        }
    }
}

void CBase::Shutdown()
{
    for (auto ritr = m_vecRunningComponents.rbegin();
         ritr != m_vecRunningComponents.rend(); ++ritr)
    {
        (*ritr)->Shutdown();
    }

    for (auto ritr = m_vecRunningComponents.rbegin();
         ritr != m_vecRunningComponents.rend(); ++ritr)
    {
        m_mapComponents.erase((*ritr)->Name());
    }
    m_vecRunningComponents.clear();
    m_vecInitializedComponents.clear();
    m_mapComponents.clear();
}

void CBase::Run()
{
    /*while (1)
    {
        std::cout<<"loop...\n";
        sleep(100);
    }*/
}

void CBase::Quit()
{
    Shutdown();
}

CBaseComponent *CBase::FindComponent(const string &name) const
{
    auto itr = m_mapComponents.find(name);
    return itr != m_mapComponents.end() ? itr->second.get() : nullptr;
}
