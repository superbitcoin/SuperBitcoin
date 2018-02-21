#include <thread>
#include <chrono>
#include <functional>
#include "base.hpp"
#include "config/argmanager.h"
#include "config/chainparamsbase.h"
#include "config/chainparams.h"

using namespace appbase;

CApp &CApp::Instance()
{
    static  CApp _app;
    return _app;
}

CApp::CApp()
    : nVersion(1), bShutdown(false), cArgs(new CArgsManager)
{
}

CApp::~CApp()
{
}

uint64_t CApp::Version() const
{
    return nVersion;
}

void CApp::SetVersion(uint64_t version)
{
    nVersion = version;
}

bool CApp::InitParams(int argc, char *argv[])
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

log4cpp::Category &CApp::mlog = log4cpp::Category::getInstance(EMTOSTR(CID_APP));

bool CApp::Startup()
{
    return ForEachComponent(true, [](IComponent* component){ return component->Startup(); });
}

bool CApp::Shutdown()
{
    return ForEachComponent<std::function<bool(IComponent*)>, ReverseContainerIterator>(false, [](IComponent* component){ return component->Shutdown(); });
}

void CApp::Run()
{
    while (!bShutdown)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
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

IComponent* CApp::FindComponent(int componentID) const
{
    auto it = m_mapComponents.find(componentID);
    return it != m_mapComponents.end() ? it->second.get() : nullptr;
}


IComponent::state CApp::GetState() const
{
    return initialized;
}

bool CApp::Initialize()
{
    return false;
}

log4cpp::Category &CApp::getLog()
{
    return mlog;
}
