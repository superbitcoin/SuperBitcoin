#include <thread>
#include <chrono>
#include <functional>
#include "base.hpp"
#include "config/argmanager.h"
#include "config/chainparamsbase.h"
#include "config/chainparams.h"

using namespace appbase;

CBase &CBase::Instance()
{
    static CBase _app;
    return _app;
}

CBase::CBase()
    : nVersion(1), bShutdown(false), cArgs(new CArgsManager)
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

bool CBase::InitParams(int argc, char *argv[])
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

bool CBase::Initialize(int argc, char **argv)
{
    return InitParams(argc, argv) && ForEachComponent(true, [](IComponent* component){ return component->Initialize(); });
}

bool CBase::Startup()
{
    return ForEachComponent(true, [](IComponent* component){ return component->Startup(); });
}

bool CBase::Shutdown()
{
    return ForEachComponent<std::function<bool(IComponent*)>, ReverseContainerIterator>(false, [](IComponent* component){ return component->Shutdown(); });
}

void CBase::Run()
{
    while (!bShutdown)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void CBase::Quit()
{
    Shutdown();
}

bool CBase::RegisterComponent(IComponent* component)
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

IComponent* CBase::FindComponent(int componentID) const
{
    auto it = m_mapComponents.find(componentID);
    return it != m_mapComponents.end() ? it->second.get() : nullptr;
}
