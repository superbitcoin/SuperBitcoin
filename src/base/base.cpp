#include <thread>
#include <chrono>
#include <functional>
#include <signal.h>
#include <boost/interprocess/sync/file_lock.hpp>
#include <log4cpp/PropertyConfigurator.hh>
#include <log4cpp/PatternLayout.hh>
#include <log4cpp/RollingFileAppender.hh>
#include <log4cpp/OstreamAppender.hh>
#include "base.hpp"
#include "noui.h"
#include "config/chainparamsbase.h"
#include "config/chainparams.h"
#include "compat/sanity.h"
#include "config/sbtc-config.h"
#include "sbtccore/clientversion.h"
#include "utils/util.h"
#include "utils/utilstrencodings.h"
#include "rpc/server.h"
#include "p2p/net.h"
#include "p2p/netbase.h"
#include "utils/arith_uint256.h"
#include "utils/utilmoneystr.h"
#include "utils/net/torcontrol.h"
#include "wallet/key.h"
#include "wallet/feerate.h"
#include "sbtccore/block/validation.h"
#include "sbtccore/transaction/policy.h"
#include "config/consensus.h"
#include "framework/validationinterface.h"

using namespace appbase;

log4cpp::Category &IBaseApp::mlog = log4cpp::Category::getInstance(EMTOSTR(CID_APP));
std::unique_ptr<CArgsManager> appbase::IBaseApp::pArgs = std::make_unique<CArgsManager>();
std::unique_ptr<CChainParams> appbase::IBaseApp::pChainParams = std::make_unique<CChainParams>();

const CArgsManager &Args()
{
    return appbase::IBaseApp::GetArgsManager();
}

const CChainParams &Params()
{
    return appbase::IBaseApp::GetChainParams();
}

bool IBaseApp::InitializeLogging(fs::path path)
{
    bool bOk = true;
    try
    {
        log4cpp::PropertyConfigurator::configure((path / fs::path("log.conf")).string().c_str());
    } catch (log4cpp::ConfigureFailure &f)
    {
        std::cout << f.what() << std::endl;
        std::cout << "using default log conf" << std::endl;
        bOk = false;
    }

    if (!bOk)
    {
        try
        {
            log4cpp::PatternLayout *pLayout1 = new log4cpp::PatternLayout();//创建一个Layout;
            pLayout1->setConversionPattern("%d: %p  %x: %m%n");//指定布局格式;

            log4cpp::PatternLayout *pLayout2 = new log4cpp::PatternLayout();
            pLayout2->setConversionPattern("%d: %p  %x: %m%n");

            log4cpp::RollingFileAppender *rollfileAppender = new log4cpp::RollingFileAppender(
                    "rollfileAppender", (path / fs::path("sbtc.log")).string().c_str(), 100 * 1024, 1);
            rollfileAppender->setLayout(pLayout1);
            log4cpp::Category &root = log4cpp::Category::getRoot().getInstance("RootName");//从系统中得到Category的根;
            root.addAppender(rollfileAppender);
            root.setPriority(log4cpp::Priority::NOTICE);//设置Category的优先级;
            log4cpp::OstreamAppender *osAppender = new log4cpp::OstreamAppender("osAppender", &std::cout);
            osAppender->setLayout(pLayout2);
            root.addAppender(osAppender);
            root.notice("log conf is using defalt !");

            log4cpp::Category &mlog = log4cpp::Category::getInstance(EMTOSTR(CID_APP));
            mlog.addAppender(rollfileAppender);
            mlog.setPriority(log4cpp::Priority::NOTICE);//设置Category的优先级;
            mlog.addAppender(osAppender);
            mlog_notice("CID_APP log conf is using defalt !");

        } catch (...)
        {
            return false;
        }
    }

    return true;
}

static std::string LicenseInfo()
{
    const std::string URL_SOURCE_CODE = "<https://github.com/bitcoin/bitcoin>";
    const std::string URL_WEBSITE = "<https://bitcoincore.org>";

    return CopyrightHolders(strprintf(_("Copyright (C) %i-%i"), 2009, COPYRIGHT_YEAR) + " ") + "\n" +
           "\n" +
           strprintf(_("Please contribute if you find %s useful. "
                               "Visit %s for further information about the software."),
                     PACKAGE_NAME, URL_WEBSITE) +
           "\n" +
           strprintf(_("The source code is available from %s."),
                     URL_SOURCE_CODE) +
           "\n" +
           "\n" +
           _("This is experimental software.") + "\n" +
           strprintf(_("Distributed under the MIT software license, see the accompanying file %s or %s"), "COPYING",
                     "<https://opensource.org/licenses/MIT>") + "\n" +
           "\n" +
           strprintf(
                   _("This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit %s and cryptographic software written by Eric Young and UPnP software written by Thomas Bernard."),
                   "<https://www.openssl.org>") +
           "\n";
}

static void PrintVersion()
{
    std::cout << strprintf(_("%s Daemon"), _(PACKAGE_NAME)) + " " + _("version") + " " + FormatFullVersion() + "\n" +
                 FormatParagraph(LicenseInfo()) << std::endl;
}

IBaseApp::IBaseApp() : nVersion(1), bShutdown(false)
{

}

bool IBaseApp::ParamsInitialize(int argc, char **argv)
{
    InitOptionMap();
    if (!pArgs->Init(argc, argv))
    {
        return false;
    }


    InitializeLogging(pArgs->GetDataDir(false));

    if (pArgs->IsArgSet("help") || pArgs->IsArgSet("usage"))
    {
        std::cout << pArgs->GetHelpMessage();
        return false;
    }

    if (pArgs->IsArgSet("version"))
    {
        PrintVersion();
        return false;
    }

    try
    {
        if (!fs::is_directory(pArgs->GetDataDir(false)))
        {
            mlog_error("Error: Specified data directory \"%s\" does not exist.",
                       pArgs->GetArg<std::string>("-datadir", "").c_str());
            return false;
        }

        pArgs->ReadConfigFile(pArgs->GetArg<std::string>("-conf", BITCOIN_CONF_FILENAME));

        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        pChainParams = CreateChainParams(ChainNameFromCommandLine());
    }
    catch (const std::exception &e)
    {
        mlog_error("Error: %s.", e.what());
        return false;
    }
    catch (...)
    {
        mlog_error("Error: initParams!");
        return false;
    }
    return true;
}

bool IBaseApp::Initialize(int argc, char **argv)
{
    return ParamsInitialize(argc, argv) && AppInitialize() && ComponentInitialize();
}

bool IBaseApp::ComponentInitialize()
{
    return ForEachComponent(true, [](IComponent *component)
    { return component->Initialize(); });
}

bool IBaseApp::Startup()
{
    return ForEachComponent(true, [](IComponent *component)
    { return component->Startup(); });
}

bool IBaseApp::Shutdown()
{
    bool fRet = ForEachComponent<std::function<bool(IComponent *)>, ReverseContainerIterator>(false,
                                                                                              [](IComponent *component)
                                                                                              { return component->Shutdown(); });

    return fRet;
}

bool IBaseApp::Run()
{
    while (!bShutdown)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    return true;
}

bool IBaseApp::RegisterComponent(IComponent *component)
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