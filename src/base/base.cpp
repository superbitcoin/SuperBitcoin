#include <thread>
#include <chrono>
#include <functional>
#include <signal.h>
#include <boost/interprocess/sync/file_lock.hpp>
#include "utils/util.h"
#include <log4cpp/PropertyConfigurator.hh>
#include <log4cpp/PatternLayout.hh>
#include <log4cpp/RollingFileAppender.hh>
#include <log4cpp/OstreamAppender.hh>
#include "base.hpp"
#include "noui.h"
#include "config/chainparams.h"
#include "compat/sanity.h"
#include "config/sbtc-config.h"
#include "sbtccore/clientversion.h"

#include "utils/utilstrencodings.h"
#include "rpc/server.h"
#include "p2p/net.h"
#include "p2p/netbase.h"
#include "utils/arith_uint256.h"
#include "utils/net/torcontrol.h"
#include "sbtccore/block/validation.h"
#include "sbtccore/transaction/policy.h"
#include "framework/validationinterface.h"

using namespace appbase;

std::unique_ptr<CArgsManager> appbase::IBaseApp::pArgs = std::make_unique<CArgsManager>();
std::unique_ptr<CChainParams> appbase::IBaseApp::pChainParams = std::make_unique<CChainParams>();

const CArgsManager &Args()
{
    return *IBaseApp::pArgs.get();
}

const CChainParams &Params()
{
    return *IBaseApp::pChainParams.get();
}

static bool InitializeLogging(fs::path path)
{

    fs::path logPath = path / "log";
    if (!fs::is_directory(logPath))
    {
        fs::create_directories(logPath);
    }
    // set logpath for the log4cpp to get the log directories
    setenv("logpath", logPath.string().c_str(), 1);

    bool bOk = true;
    try
    {
        log4cpp::PropertyConfigurator::configure((path / fs::path("log.conf")).string().c_str());
    } catch (log4cpp::ConfigureFailure &f)
    {
        std::cout << f.what() << std::endl;
        std::cout << "using default log conf" << __FILE__ << __LINE__ << std::endl;
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
            root.notice("log conf is using default !");

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

IBaseApp::IBaseApp() noexcept : nVersion(1), bShutdown(false)
{
}

bool IBaseApp::Initialize(int argc, char **argv)
{
    SetupEnvironment();

    InitOptionMap();

    if (!pArgs->Init(argc, argv))
    {
        return false;
    }

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

    InitializeLogging(pArgs->GetDataDir(false));

    PrintAppStartupInfo();

    try
    {
        if (!fs::is_directory(pArgs->GetDataDir(false)))
        {
            ELogFormat("Error: Specified data directory \"%s\" does not exist.",
                       pArgs->GetArg<std::string>("-datadir", ""));
            return false;
        }

        pArgs->ReadConfigFile(pArgs->GetArg<std::string>("-conf", BITCOIN_CONF_FILENAME));

        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        pChainParams = CreateChainParams(ChainNameFromCommandLine());
    }
    catch (const std::exception &e)
    {
        ELogFormat("Read config file exception: %s.", e.what());
        return false;
    }
    catch (...)
    {
        ELogFormat("Read config file exception!");
        return false;
    }

    if (!SetupNetworking())
    {
        ELogFormat("Initializing networking failed.");
        return false;
    }

    return true;
}

bool IBaseApp::Startup()
{
    return true;
}

bool IBaseApp::Run()
{
    return true;
}

bool IBaseApp::Shutdown()
{
    return true;
}

void IBaseApp::PrintAppStartupInfo()
{
    //NOOP
}

IComponent *IBaseApp::FindComponent(int id) const
{
    return nullptr;
}

CScheduler &IBaseApp::GetScheduler()
{
    assert(false); // This method should never be called.
    return *reinterpret_cast<CScheduler *>(0x1);
}

CEventManager &IBaseApp::GetEventManager()
{
    assert(false); // This method should never be called.
    return *reinterpret_cast<CEventManager *>(0x1);
}

CClientUIInterface &IBaseApp::GetUIInterface()
{
    assert(false); // This method should never be called.
    return *reinterpret_cast<CClientUIInterface *>(0x1);
}

