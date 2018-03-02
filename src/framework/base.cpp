#include <thread>
#include <chrono>
#include <functional>
#include <signal.h>
#include <boost/interprocess/sync/file_lock.hpp>
#include "base.hpp"
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

log4cpp::Category &CApp::mlog = log4cpp::Category::getInstance(EMTOSTR(CID_APP));

CApp &CApp::Instance()
{
    static CApp _app;
    return _app;
}

CApp::CApp()
        : nVersion(1), bShutdown(false)
{
}

// Parameter interaction based on rules
void CApp::InitParameterInteraction()
{
    // when specifying an explicit binding address, you want to listen on it
    // even when -connect or -proxy is specified
    if (gArgs.IsArgSet("-bind"))
    {
        if (gArgs.SoftSetArg("-isten", true))
            mlog.notice("%s: parameter interaction: -bind set -> setting -listen=1\n", __func__);
    }
    if (gArgs.IsArgSet("-whitebind"))
    {
        if (gArgs.SoftSetArg("-listen", true))
            mlog.notice("%s: parameter interaction: -whitebind set -> setting -listen=1\n", __func__);
    }

    if (gArgs.IsArgSet("-connect"))
    {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (gArgs.SoftSetArg("-dnsseed", false))
            mlog.notice("%s: parameter interaction: -connect set -> setting -dnsseed=0\n", __func__);
        if (gArgs.SoftSetArg("-listen", false))
            mlog.notice("%s: parameter interaction: -connect set -> setting -listen=0\n", __func__);
    }

    if (gArgs.IsArgSet("-proxy"))
    {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (gArgs.SoftSetArg("-listen", false))
            mlog.notice("%s: parameter interaction: -proxy set -> setting -listen=0\n", __func__);
        // to protect privacy, do not use UPNP when a proxy is set. The user may still specify -listen=1
        // to listen locally, so don't rely on this happening through -listen below.
        if (gArgs.SoftSetArg("-upnp", false))
            mlog.notice("%s: parameter interaction: -proxy set -> setting -upnp=0\n", __func__);
        // to protect privacy, do not discover addresses by default
        if (gArgs.SoftSetArg("-discover", false))
            mlog.notice("%s: parameter interaction: -proxy set -> setting -discover=0\n", __func__);
    }

    if (!gArgs.GetArg<bool>("-listen", DEFAULT_LISTEN))
    {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        if (gArgs.SoftSetArg("-upnp", false))
            mlog.notice("%s: parameter interaction: -listen=0 -> setting -upnp=0\n", __func__);
        if (gArgs.SoftSetArg("-discover", false))
            mlog.notice("%s: parameter interaction: -listen=0 -> setting -discover=0\n", __func__);
        if (gArgs.SoftSetArg("-listenonion", false))
            mlog.notice("%s: parameter interaction: -listen=0 -> setting -listenonion=0\n", __func__);
    }

    if (gArgs.IsArgSet("-externalip"))
    {
        // if an explicit public IP is specified, do not try to find others
        if (gArgs.SoftSetArg("-discover", false))
            mlog.notice("%s: parameter interaction: -externalip set -> setting -discover=0\n", __func__);
    }

    // disable whitelistrelay in blocksonly mode
    if (gArgs.GetArg<bool>("-blocksonly", DEFAULT_BLOCKSONLY))
    {
        if (gArgs.SoftSetArg("-whitelistrelay", false))
            mlog.notice("%s: parameter interaction: -blocksonly=1 -> setting -whitelistrelay=0\n", __func__);
    }

    // Forcing relay from whitelisted hosts implies we will accept relays from them in the first place.
    if (gArgs.GetArg<bool>("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY))
    {
        if (gArgs.SoftSetArg("-whitelistrelay", true))
            mlog.notice("%s: parameter interaction: -whitelistforcerelay=1 -> setting -whitelistrelay=1\n", __func__);
    }

    if (gArgs.IsArgSet("-blockmaxsize"))
    {
        unsigned int max_size = gArgs.GetArg<uint32_t>("-blockmaxsize", 0U);
        if (gArgs.SoftSetArg("-blockmaxweight", (uint32_t)(max_size * WITNESS_SCALE_FACTOR)))
        {
            mlog.notice(
                    "%s: parameter interaction: -blockmaxsize=%d -> setting -blockmaxweight=%d (-blockmaxsize is deprecated!)\n",
                    __func__, max_size, max_size * WITNESS_SCALE_FACTOR);
        } else
        {
            mlog.notice("%s: Ignoring blockmaxsize setting which is overridden by blockmaxweight", __func__);
        }
    }
}

static void HandleSIGTERM(int)
{
    app().RequestShutdown();
}

bool CApp::AppInitBasicSetup()
{
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0));
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
    // Enable Data Execution Prevention (DEP)
    // Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
    // A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
    // We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >= 0x0601 (Windows 7),
    // which is not correct. Can be removed, when GCCs winbase.h is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
    typedef BOOL (WINAPI *PSETPROCDEPPOL)(DWORD);
    PSETPROCDEPPOL setProcDEPPol = (PSETPROCDEPPOL)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
    if (setProcDEPPol != nullptr) setProcDEPPol(PROCESS_DEP_ENABLE);
#endif

    if (!SetupNetworking())
    {
        mlog.error("Initializing networking failed");
        return false;
    }

#ifndef WIN32
    //    const CArgsManager &appArgs = app().GetArgsManager();
    //    if (!appArgs.GetArg<bool>("sysperms", false))
    //        umask(077);

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
    return true;
}

bool CApp::AppInitParameterInteraction()
{
    const CChainParams &chainparams = GetChainParams();

    // also see: InitParameterInteraction()

    // if using block pruning, then disallow txindex
    if (gArgs.GetArg<int32_t>("-prune", 0))
    {
        if (gArgs.GetArg<bool>("-txindex", DEFAULT_TXINDEX))
        {
            mlog.error("Prune mode is incompatible with -txindex.");
            return false;
        }
    }

    // -bind and -whitebind can't be set when not listening
    size_t nUserBind = gArgs.GetArgs("-bind").size() + gArgs.GetArgs("-whitebind").size();
    if (nUserBind != 0 && !gArgs.GetArg<bool>("listen", DEFAULT_LISTEN))
    {
        mlog.error("Cannot set -bind or -whitebind together with -listen=0");
        return false;
    }

    if (gArgs.IsArgSet("-debug"))
    {
        // Special-case: if -debug=0/-nodebug is set, turn off debugging messages
        const std::vector<std::string> categories = gArgs.GetArgs("-debug");

        if ((find(categories.begin(), categories.end(), std::string("no")) == categories.end()) ||
            find(categories.begin(), categories.end(), std::string("n")) == categories.end())
        {
            for (const auto &cat : categories)
            {
                uint32_t flag = 0;
                if (!GetLogCategory(&flag, &cat))
                {
                    mlog.warn("Unsupported logging category %s=%s.", "-debug", cat);
                    continue;
                }
                logCategories |= flag;
            }
        }
    }

    // Now remove the logging categories which were explicitly excluded
    for (const std::string &cat : gArgs.GetArgs("-debugexclude"))
    {
        uint32_t flag = 0;
        if (!GetLogCategory(&flag, &cat))
        {
            mlog.warn("Unsupported logging category %s=%s.", "-debugexclude", cat);
            continue;
        }
        logCategories &= ~flag;
    }

    // Check for -debugnet
    if (gArgs.GetArg<bool>("-debugnet", false))
        mlog.warn("Unsupported argument -debugnet ignored, use -debug=net.");

    // Check for -socks - as this is a privacy risk to continue, exit here
    if (gArgs.IsArgSet("-socks"))
    {
        mlog.error(
                "Unsupported argument -socks found. Setting SOCKS version isn't possible anymore, only SOCKS5 proxies are supported.");
        return false;
    }

    // Check for -tor - as this is a privacy risk to continue, exit here
    if (gArgs.GetArg<bool>("-tor", false))
    {
        mlog.error("Unsupported argument -tor found, use -onion.");
        return false;
    }

    if (gArgs.GetArg<bool>("-benchmark", false))
        mlog.warn("Unsupported argument -benchmark ignored, use -debug=bench.");

    if (gArgs.GetArg<bool>("-whitelistalwaysrelay", false))
        mlog.warn(
                "Unsupported argument -whitelistalwaysrelay ignored, use -whitelistrelay and/or -whitelistforcerelay.");

    if (gArgs.IsArgSet("-blockminsize"))
        mlog.warn("Unsupported argument -blockminsize ignored.");

    //TODO: mempool component init.
    //    // Checkmempool and checkblockindex default to true in regtest mode
    //    int ratio = std::min<int>(
    //            std::max<int>(gArgs.GetArg<int32_t>("-checkmempool", chainparams.DefaultConsistencyChecks() ? 1 : 0), 0),
    //            1000000);
    //    if (ratio != 0)
    //    {
    //        mempool.setSanityCheck(1.0 / ratio);
    //    }

    //TODO: remove these temp variables.
    //    fCheckBlockIndex = gArgs.GetArg<bool>("-checkblockindex", chainparams.DefaultConsistencyChecks());
    //    fCheckpointsEnabled = gArgs.GetArg<bool>("-checkpoints", DEFAULT_CHECKPOINTS_ENABLED);

    uint256 hashAssumeValid = uint256S(
            gArgs.GetArg<std::string>("-assumevalid", chainparams.GetConsensus().defaultAssumeValid.GetHex()));
    if (!hashAssumeValid.IsNull())
        mlog.notice("Assuming ancestors of block %s have valid signatures.", hashAssumeValid.GetHex());
    else
        mlog.notice("Validating signatures for all blocks.");

    if (gArgs.IsArgSet("-minimumchainwork"))
    {
        const std::string minChainWorkStr = gArgs.GetArg<std::string>("-minimumchainwork", "");
        if (!IsHexNumber(minChainWorkStr))
        {
            mlog.error("Invalid non-hex (%s) minimum chain work value specified", minChainWorkStr);
            return false;
        }
        nMinimumChainWork = UintToArith256(uint256S(minChainWorkStr));
    } else
    {
        nMinimumChainWork = UintToArith256(chainparams.GetConsensus().nMinimumChainWork);
    }
    mlog.notice("Setting nMinimumChainWork=%s.", nMinimumChainWork.GetHex());
    if (nMinimumChainWork < UintToArith256(chainparams.GetConsensus().nMinimumChainWork))
    {
        mlog.warn("Warning: nMinimumChainWork set below default value of %s.",
                  chainparams.GetConsensus().nMinimumChainWork.GetHex());
    }

    // mempool limits
    int64_t nMempoolSizeMax = gArgs.GetArg<uint32_t>("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    int64_t nMempoolSizeMin =
            gArgs.GetArg<uint32_t>("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000 * 40;
    if (nMempoolSizeMax < 0 || nMempoolSizeMax < nMempoolSizeMin)
    {
        mlog.error("-maxmempool must be at least %d MB", std::ceil(nMempoolSizeMin / 1000000.0));
        return false;
    }

    // incremental relay fee sets the minimum feerate increase necessary for BIP 125 replacement in the mempool
    // and the amount the mempool min fee increases above the feerate of txs evicted due to mempool limiting.
    if (gArgs.IsArgSet("-incrementalrelayfee"))
    {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg<std::string>("-incrementalrelayfee", ""), n))
        {
            mlog.error(
                    AmountErrMsg("incrementalrelayfee", gArgs.GetArg<std::string>("-incrementalrelayfee", "")));
            return false;
        }

        incrementalRelayFee = CFeeRate(n);
    }

    // block pruning; get the amount of disk space (in MiB) to allot for block & undo files
    int64_t nPruneArg = gArgs.GetArg<int32_t>("-prune", 0);
    if (nPruneArg < 0)
    {
        mlog.error("Prune cannot be configured with a negative value.");
        return false;
    }
    nPruneTarget = (uint64_t)nPruneArg * 1024 * 1024;
    if (nPruneArg == 1)
    {  // manual pruning: -prune=1
        mlog.notice(
                "Block pruning enabled.  Use RPC call pruneblockchain(height) to manually prune block and undo files.");
        nPruneTarget = std::numeric_limits<uint64_t>::max();
        fPruneMode = true;
    } else if (nPruneTarget)
    {
        if (nPruneTarget < MIN_DISK_SPACE_FOR_BLOCK_FILES)
        {
            mlog.error("Prune configured below the minimum of %d MiB.  Please use a higher number.",
                       MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024);
            return false;
        }
        mlog.notice("Prune configured to target %uMiB on disk for block and undo files.", nPruneTarget / 1024 / 1024);
        fPruneMode = true;
    }

    nConnectTimeout = gArgs.GetArg<int32_t>("-timeout", DEFAULT_CONNECT_TIMEOUT);
    if (nConnectTimeout <= 0)
        nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;

    if (gArgs.IsArgSet("-minrelaytxfee"))
    {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg<std::string>("-minrelaytxfee", ""), n))
        {
            mlog.error(AmountErrMsg("minrelaytxfee", gArgs.GetArg<std::string>("-minrelaytxfee", "")));
            return false;
        }
        // High fee check is done afterward in CWallet::ParameterInteraction()
        ::minRelayTxFee = CFeeRate(n);
    } else if (incrementalRelayFee > ::minRelayTxFee)
    {
        // Allow only setting incrementalRelayFee to control both
        ::minRelayTxFee = incrementalRelayFee;
        mlog.notice("Increasing minrelaytxfee to %s to match incrementalrelayfee.", ::minRelayTxFee.ToString());
    }

    // Sanity check argument for min fee for including tx in block
    // TODO: Harmonize which arguments need sanity checking and where that happens
    if (gArgs.IsArgSet("-blockmintxfee"))
    {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg<std::string>("-blockmintxfee", ""), n))
        {
            mlog.error(AmountErrMsg("blockmintxfee", gArgs.GetArg<std::string>("-blockmintxfee", "")));
            return false;
        }
    }

    // Feerate used to define dust.  Shouldn't be changed lightly as old
    // implementations may inadvertently create non-standard transactions
    if (gArgs.IsArgSet("-dustrelayfee"))
    {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg<std::string>("-dustrelayfee", ""), n) || 0 == n)
        {
            mlog.error(AmountErrMsg("dustrelayfee", gArgs.GetArg<std::string>("-dustrelayfee", "")));
            return false;
        }
        dustRelayFee = CFeeRate(n);
    }

    fRequireStandard = !gArgs.GetArg<bool>("-acceptnonstdtxn", !chainparams.RequireStandard());
    if (chainparams.RequireStandard() && !fRequireStandard)
    {
        mlog.error(
                "acceptnonstdtxn is not currently supported for %s chain", chainparams.NetworkIDString());
        return false;
    }

    nBytesPerSigOp = gArgs.GetArg<uint32_t>("-bytespersigop", nBytesPerSigOp);

    //#ifdef ENABLE_WALLET
    //    if (!CWallet::ParameterInteraction())
    //        return false;
    //#endif

    fIsBareMultisigStd = gArgs.GetArg<bool>("-permitbaremultisig", DEFAULT_PERMIT_BAREMULTISIG);
    fAcceptDatacarrier = gArgs.GetArg<bool>("-datacarrier", DEFAULT_ACCEPT_DATACARRIER);
    nMaxDatacarrierBytes = gArgs.GetArg<uint32_t>("-datacarriersize", nMaxDatacarrierBytes);

    // Option to startup with mocktime set (used for regression testing):
    SetMockTime(gArgs.GetArg<int32_t>("-mocktime", 0)); // SetMockTime(0) is a no-op

    if (gArgs.GetArg<bool>("-peerbloomfilters", DEFAULT_PEERBLOOMFILTERS))
        nLocalServices = ServiceFlags(nLocalServices | NODE_BLOOM);

    if (gArgs.GetArg<uint32_t>("-rpcserialversion", DEFAULT_RPC_SERIALIZE_VERSION) < 0)
    {
        mlog.error("rpcserialversion must be non-negative.");
        return false;
    }

    if (gArgs.GetArg<uint32_t>("-rpcserialversion", DEFAULT_RPC_SERIALIZE_VERSION) > 1)
    {
        mlog.error("unknown rpcserialversion requested.");
        return false;
    }

    nMaxTipAge = gArgs.GetArg<int64_t>("maxtipage", DEFAULT_MAX_TIP_AGE);

    fEnableReplacement = gArgs.GetArg<bool>("-mempoolreplacement", DEFAULT_ENABLE_REPLACEMENT);

    if (gArgs.IsArgSet("-vbparams"))
    {
        // Allow overriding version bits parameters for testing
        if (!chainparams.MineBlocksOnDemand())
        {
            mlog.error("Version bits parameters may only be overridden on regtest.");
            return false;
        }
        for (const std::string &strDeployment : gArgs.GetArgs("-vbparams"))
        {
            std::vector<std::string> vDeploymentParams;
            boost::split(vDeploymentParams, strDeployment, boost::is_any_of(":"));
            if (vDeploymentParams.size() != 3)
            {
                mlog.error("Version bits parameters malformed, expecting deployment:start:end");
                return false;
            }
            int64_t nStartTime, nTimeout;
            if (!ParseInt64(vDeploymentParams[1], &nStartTime))
            {
                mlog.error("Invalid nStartTime (%s)", vDeploymentParams[1]);
                return false;
            }
            if (!ParseInt64(vDeploymentParams[2], &nTimeout))
            {
                mlog.error("Invalid nTimeout (%s)", vDeploymentParams[2]);
                return false;
            }
            bool found = false;
            for (int j = 0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j)
            {
                if (vDeploymentParams[0].compare(VersionBitsDeploymentInfo[j].name) == 0)
                {
                    UpdateVersionBitsParameters(Consensus::DeploymentPos(j), nStartTime, nTimeout);
                    found = true;
                    mlog.notice("Setting version bits activation parameters for %s to start=%ld, timeout=%ld\n",
                                vDeploymentParams[0], nStartTime, nTimeout);
                    break;
                }
            }
            if (!found)
            {
                mlog.error("Invalid deployment (%s)", vDeploymentParams[0]);
                return false;
            }
        }
    }
    return true;
}

static bool InitSanityCheck(void)
{
    if (!ECC_InitSanityCheck())
    {
        CApp::mlog.error("Elliptic curve cryptography sanity check failure. Aborting.");
        return false;
    }

    if (!glibc_sanity_test() || !glibcxx_sanity_test())
        return false;

    if (!Random_SanityCheck())
    {
        CApp::mlog.error("OS cryptographic RNG sanity check failure. Aborting.");
        return false;
    }

    return true;
}

static bool LockDataDirectory(bool probeOnly)
{
    std::string strDataDir = GetDataDir().string();

    // Make sure only a single Super Bitcoin process is using the data directory.
    fs::path pathLockFile = GetDataDir() / ".lock";
    FILE *file = fsbridge::fopen(pathLockFile, "a"); // empty lock file; created if it doesn't exist.
    if (file)
        fclose(file);

    try
    {
        static boost::interprocess::file_lock lock(pathLockFile.string().c_str());
        if (!lock.try_lock())
        {
            CApp::mlog.error("Cannot obtain a lock on data directory %s. %s is probably already running.",
                             strDataDir, PACKAGE_NAME);
            return false;
        }
        if (probeOnly)
        {
            lock.unlock();
        }
    } catch (const boost::interprocess::interprocess_exception &e)
    {
        CApp::mlog.error(
                "Cannot obtain a lock on data directory %s. %s is probably already running. %s.",
                strDataDir, _(PACKAGE_NAME), e.what());
        return false;
    }
    return true;
}

bool CApp::AppInitSanityChecks()
{
    // Initialize elliptic curve code
    std::string sha256_algo = SHA256AutoDetect();
    mlog.notice("Using the '%s' SHA256 implementation.", sha256_algo);
    RandomInit();
    ECC_Start();
    globalVerifyHandle.reset(new ECCVerifyHandle());

    // Sanity check
    if (!InitSanityCheck())
    {
        mlog.error("Initialization sanity check failed. %s is shutting down.", PACKAGE_NAME);
        return false;
    }

    // Probe the data directory lock to give an early error message, if possible
    // We cannot hold the data directory lock here, as the forking for daemon() hasn't yet happened,
    // and a fork will cause weird behavior to it.
    return LockDataDirectory(true);
}

bool CApp::AppInitLockDataDirectory()
{
    // After daemonization get the data directory lock again and hold on to it until exit
    // This creates a slight window for a race condition to happen, however this condition is harmless: it
    // will at most make us exit without printing a message to console.
    if (!LockDataDirectory(false))
    {
        // Detailed error printed inside LockDataDirectory
        return false;
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

bool CApp::AppInitialize(int argc, char *argv[])
{

    if (!gArgs.Init(argc, argv))
    {
        return false;
    }

    if (gArgs.IsArgSet("help") || gArgs.IsArgSet("usage"))
    {
        std::cout << gArgs.GetHelpMessage();
        return false;
    }

    if (gArgs.IsArgSet("version"))
    {
        PrintVersion();
        return false;
    }

    try
    {
        if (!fs::is_directory(gArgs.GetDataDir(false)))
        {
            mlog.error("Error: Specified data directory \"%s\" does not exist.",
                       gArgs.GetArg<std::string>("-datadir", "").c_str());
            return false;
        }

        try
        {
            gArgs.ReadConfigFile(gArgs.GetArg<std::string>("-conf", BITCOIN_CONF_FILENAME));
        }
        catch (const std::exception &e)
        {
            mlog.error("Error reading configuration file: %s.", e.what());
            return false;
        }

        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        try
        {
            bool fRegTest = gArgs.IsArgSet("-regtest");
            bool fTestNet = gArgs.IsArgSet("-testnet");
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
        gArgs.SoftSetArg("-server", true);

        // Set this early so that parameter interactions go to console
        // InitLogging();
        InitParameterInteraction();
        if (!AppInitBasicSetup())
        {
            // InitError will have been called with detailed error, which ends up on console
            exit(EXIT_FAILURE);
        }
        if (!AppInitParameterInteraction())
        {
            // InitError will have been called with detailed error, which ends up on console
            exit(EXIT_FAILURE);
        }
        if (!AppInitSanityChecks())
        {
            // InitError will have been called with detailed error, which ends up on console
            exit(EXIT_FAILURE);
        }
        if (gArgs.GetArg<bool>("-daemon", false))
        {
#if HAVE_DECL_DAEMON
            mlog.notice("Bitcoin server starting.");

            // Daemonize
            if (daemon(1, 0))
            { // don't chdir (1), do close FDs (0)
                mlog.error("Error: daemon() failed: %s.", strerror(errno));
                return false;
            }
#else
            mlog.error("Error: -daemon is not supported on this operating system.");
            return false;
#endif // HAVE_DECL_DAEMON
        }
        // Lock data directory after daemonization
        if (!AppInitLockDataDirectory())
        {
            // If locking the data directory failed, exit immediately
            exit(EXIT_FAILURE);
        }

        //avoid mutity call
        assert(scheduler == nullptr);
        assert(eventManager == nullptr);
        assert(uiInterface == nullptr);
        scheduler = std::make_unique<CScheduler>();
        eventManager = std::make_unique<CEventManager>();
        uiInterface = std::make_unique<CClientUIInterface>();

        schedulerThread = std::thread(&CScheduler::serviceQueue, scheduler.get());

        GetMainSignals().RegisterBackgroundSignalScheduler(GetScheduler());
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "CApp::initParams");
    }
    catch (...)
    {
        PrintExceptionContinue(nullptr, "CApp::initParams");
    }

#ifndef WIN32
    CreatePidFile(GetPidFile(), getpid());
#endif

    if (gArgs.GetArg<bool>("-shrinkdebugfile", logCategories == BCLog::NONE))
    {
        // Do this first since it both loads a bunch of debug.log into memory,
        // and because this needs to happen before any other debug.log printing
        ShrinkDebugFile();
    }

    mlog.notice("Default data directory %s.", GetDefaultDataDir().string());
    mlog.notice("Using data directory %s.", GetDataDir().string());
    mlog.notice("Using config file %s.",
                GetConfigFile(gArgs.GetArg<std::string>("conf", std::string(BITCOIN_CONF_FILENAME))).string());

    return true;
}

bool CApp::ComponentInitialize()
{
    return ForEachComponent(true, [](IComponent *component)
    { return component->Initialize(); });
}

bool CApp::Initialize(int argc, char **argv)
{
    return AppInitialize(argc, argv) && ComponentInitialize();
}

bool CApp::Startup()
{
    return ForEachComponent(true, [](IComponent *component)
    { return component->Startup(); });
}

bool CApp::Shutdown()
{
    bool fRet = ForEachComponent<std::function<bool(IComponent *)>, ReverseContainerIterator>(false,
                                                                                              [](IComponent *component)
                                                                                              { return component->Shutdown(); });

#ifndef WIN32
    try
    {
        fs::remove(GetPidFile());
    } catch (const fs::filesystem_error &e)
    {
        mlog.warn("%s: Unable to remove pidfile: %s.", __func__, e.what());
    }
#endif

    if (scheduler)
    {
        scheduler->stop();
        if (schedulerThread.joinable())
        {
            schedulerThread.join();
        }
    }

    globalVerifyHandle.reset();
    ECC_Stop();
    mlog.notice("%s: done.", __func__);

    GetMainSignals().UnregisterBackgroundSignalScheduler();
    return fRet;
}

bool CApp::Run()
{
    while (!bShutdown)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    return true;
}

bool CApp::RegisterComponent(IComponent *component)
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

