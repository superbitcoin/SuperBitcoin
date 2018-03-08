///////////////////////////////////////////////////////////
//  argmanager.cpp
//  Implementation of the Class CArgsManager
//  Created on:      29-2-2018 11:38:05
//  Original author: marco
///////////////////////////////////////////////////////////

#include "base.hpp"
#include "argmanager.h"
#include "chainparams.h"
#include "chainparamsbase.h"
#include "chaincontrol/checkpoints.h"

#include "sbtccore/clientversion.h"
#include "compat/compat.h"
#include "framework/sync.h"
#include "framework/init.h"
#include "framework/noui.h"
#include "framework/scheduler.h"
#include "utils/util.h"
#include "utils/net/httpserver.h"
#include "utils/net/httprpc.h"
#include "utils/utilstrencodings.h"


#include "transaction/txdb.h"
#include "p2p/net_processing.h"
#include "sbtccore/transaction/policy.h"
#include "block/validation.h"
#include "p2p/netbase.h"
#include "utils/net/torcontrol.h"
#include "script/sigcache.h"
#include "utils/utilmoneystr.h"
#include "script/standard.h"
#include "rpc/protocol.h"
#include "wallet/wallet.h"
#include "wallet/db.h"
#include "wallet/walletdb.h"
#include "framework/init.h"
#include "rpc/protocol.h"

CArgsManager gArgs;

CArgsManager::~CArgsManager()
{

}

const CArgsManager &Args()
{
    return appbase::CApp::Instance().GetArgsManager();
}

void CArgsManager::ForceSetArg(const std::string &strArg, const std::string &strValue)
{
    LOCK(cs_args);
    std::string tmp_strArg = SubPrefix(strArg);

    vector<string>::const_iterator ite_options_arr = find(options_arr.begin(), options_arr.end(), tmp_strArg);

    if (ite_options_arr == options_arr.end())
    {
        vm.erase(tmp_strArg);
        vm.insert(std::make_pair(tmp_strArg, bpo::variable_value(boost::any(std::string(strValue)),
                                                                 false)));    // std::pair< map<string, bpo::variable_value>::iterator, bool >

        return;
    }

    vector<string> &tmp_value_arr = vm.at(tmp_strArg).as<vector<string> >();
    tmp_value_arr.insert(tmp_value_arr.end(), strValue);
}

void CArgsManager::ForceSetArg(const std::string &strArg, const unsigned int value)
{
    LOCK(cs_args);
    std::string tmp_strArg = SubPrefix(strArg);

    if (vm.count(tmp_strArg))
    {
        vm.erase(tmp_strArg);
    }

    vm.insert(std::make_pair(tmp_strArg, bpo::variable_value(boost::any((unsigned int)value), false)));
}


const std::vector<std::string> CArgsManager::GetArgs(const std::string &strArg) const
{
    LOCK(cs_args);
    std::string tmp_strArg = SubPrefix(strArg);

    if (IsArgSet(tmp_strArg))
        return vm.at(tmp_strArg).as<vector<string> >();
    return {};
}

void CArgsManager::GenerateOptFormat(const int &argc, const char **argv, vector<string> &argv_arr_tmp,
                                     vector<const char *> &argv_arr)
{
    for (int i = 0; i != argc; i++)
    {
        if (strlen(argv[i]) > 2 && argv[i][0] == '-' && argv[i][1] != '-')
        {
            argv_arr_tmp.push_back("-" + string(argv[i]));
            continue;
        }

        argv_arr_tmp.push_back(string(argv[i]));
    }
    for (string &s : argv_arr_tmp)
    {
        argv_arr.push_back(s.c_str());
    }
}

bool CArgsManager::PreProc(std::function<void(bpo::options_description *app, bpo::variables_map &vm, int argc,
                                              const char **argv, HelpMessageMode mode)> callback,
                           bpo::options_description *app, int argc, const char **argv, HelpMessageMode mode)
{
    LOCK(cs_args);
    if (callback == nullptr || argv == nullptr || app == nullptr)
    {
        return false;
    }

    this->app_bpo = app;
    vm.clear();
    callback(app, vm, argc, argv, mode);
    return true;
}

void CArgsManager::InitPromOptions(bpo::options_description *app, bpo::variables_map &vm, int argc, const char **argv,
                                   HelpMessageMode mode)
{
    const auto defaultBaseParams = CreateBaseChainParams(CBaseChainParams::MAIN);
    const auto testnetBaseParams = CreateBaseChainParams(CBaseChainParams::TESTNET);
    const auto defaultChainParams = CreateChainParams(CBaseChainParams::MAIN);
    const auto testnetChainParams = CreateChainParams(CBaseChainParams::TESTNET);

    std::string strHead =
            strprintf(_("%s Daemon"), _(PACKAGE_NAME)) + " " + _("version") + " " + FormatFullVersion() + "\n" +
            "\n" + _("Usage:") + "\n" + "  bitcoind [options]                     " +
            strprintf(_("Start %s Daemon"), _(PACKAGE_NAME)) + "\n";
    bpo::options_description helpGroup(strHead);
    helpGroup.add_options()
            ("help,h", "Print this help message and exit")
            ("?", "Print this help message and exit")
            ("version", "Print version and exit")
            ("alertnotify", bpo::value<string>(),
             "Execute command when a relevant alert is received or we see a really long fork (%s in cmd is replaced by message)")
            ("blocknotify", bpo::value<string>(),
             "Execute command when the best block changes (%s in cmd is replaced by block hash)")
            ("blocksonly", bpo::value<string>(),
             "Whether to operate in a blocks only mode, default: no(parameters: n, no, y, yes)")  // -help-debug
            ("assumevalid", bpo::value<string>(), strprintf(
                    _("If this block is in the chain assume that it and its ancestors are valid and potentially skip their script verification (0 to verify all, default: %s, testnet: %s)"),
                    defaultChainParams->GetConsensus().defaultAssumeValid.GetHex(),
                    testnetChainParams->GetConsensus().defaultAssumeValid.GetHex()).c_str())
            ("conf", bpo::value<string>(), "Specify configuration file")

            //if mode == HMM_BITCOIND
            //#if HAVE_DECL_DAEMON
            //            ("daemon", bpo::value<string>(),
            //             "Run in the background as a daemon and accept commands(parameters: n, no, y, yes)") // dependence : mode, HAVE_DECL_DAEMON
            //#endif
            //#endif

            ("datadir", bpo::value<string>(), "Specify data directory")
            ("dbbatchsize", bpo::value<int64_t>(), "Maximum database write batch size in bytes")  // -help-debug
            ("dbcache", bpo::value<int64_t>(), "Set database cache size in megabytes")
            ("feefilter", bpo::value<string>(),
             "Tell other nodes to filter invs to us by our mempool min fee (parameters: n, no, y, yes)") // -help-debug
            ("loadblock", bpo::value<vector<string> >()->multitoken(),
             "Imports blocks from external blk000??.dat file on startup")
            ("maxorphantx", bpo::value<unsigned int>(), "Keep at most <n> unconnectable transactions in memory")
            ("maxmempool", bpo::value<unsigned int>(), "Keep the transaction memory pool below <n> megabytes")
            ("mempoolexpiry", bpo::value<unsigned int>(),
             "Do not keep transactions in the mempool longer than <n> hours")
            ("minimumchainwork", bpo::value<string>(),
             "Minimum work assumed to exist on a valid chain in hex")    // -help-debug
            ("persistmempool", bpo::value<string>(),
             "Whether to save the mempool on shutdown and load on restart (parameters: n, no, y, yes)")
            ("blockreconstructionextratxn", bpo::value<unsigned int>(),
             "Extra transactions to keep in memory for compact block reconstructions")
            ("par", bpo::value<int>(), strprintf(
                    _("Set the number of script verification threads (%u to %d, 0 = auto, <0 = leave that many cores free, default: %d)"),
                    -GetNumCores(), MAX_SCRIPTCHECK_THREADS, DEFAULT_SCRIPTCHECK_THREADS).c_str())

#ifndef WIN32
            ("pid", bpo::value<string>(), "Specify pid file")
#endif

            ("prune", bpo::value<int32_t>(), strprintf(
                    _("Reduce storage requirements by enabling pruning (deleting) of old blocks. This allows the pruneblockchain RPC to be called to delete specific blocks, and enables automatic pruning of old blocks if a target size in MiB is provided. This mode is incompatible with -txindex and -rescan. "
                              "Warning: Reverting this setting requires re-downloading the entire blockchain. "
                              "(default: 0 = disable pruning blocks, 1 = allow manual pruning via RPC, >%u = automatically prune block files to stay under the specified target size in MiB)"),
                    MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024).c_str())
            ("reindex-chainstate", bpo::value<string>(),
             "Rebuild chain state from the currently indexed blocks (parameters: n, no, y, yes)")
            ("reindex", bpo::value<string>(),
             "Rebuild chain state and block index from the blk*.dat files on disk(parameters: n, no, y, yes)")

#ifndef WIN32
            ("sysperms", bpo::value<string>(),
             "Create new files with system default permissions, instead of umask 077, only effective with disabled wallet functionality (parameters: n, no, y, yes)")
#endif

            ("txindex", bpo::value<string>(),
             "Maintain a full transaction index, used by the getrawtransaction rpc call(parameters: n, no, y, yes)");
    app->add(helpGroup);

    bpo::options_description connGroup("Connection options:");
    connGroup.add_options()
            ("addnode", bpo::value<vector<string> >()->multitoken(),
             "Add a node to connect to and attempt to keep the connection open")
            ("banscore", bpo::value<unsigned int>(), "Threshold for disconnecting misbehaving peers")
            ("bantime", bpo::value<unsigned int>(), "Number of seconds to keep misbehaving peers from reconnecting")
            ("bind", bpo::value<vector<string> >()->multitoken(),
             "Bind to given address and always listen on it. Use [host]:port notation for IPv6")
            ("connect", bpo::value<vector<string> >()->multitoken(),
             "Connect only to the specified node(s); -connect=0 disables automatic connections")
            ("discover", bpo::value<string>(),
             "Discover own IP addresses default: yes when listening and no -externalip or -proxy(parameters: n, no, y, yes)")
            ("dns", bpo::value<string>(),
             "Allow DNS lookups for -addnode, -seednode and -connect, default: yes(parameters: n, no, y, yes)")
            ("dnsseed", bpo::value<string>(),
             "Query for peer addresses via DNS lookup, if low on addresses default: yes unless -connect used(parameters: n, no, y, yes)")
            ("externalip", bpo::value<vector<string> >()->multitoken(), "Specify your own public address")
            ("forcednsseed", bpo::value<string>(),
             "Always query for peer addresses via DNS lookup default: no(parameters: n, no, y, yes)")
            ("listen", bpo::value<string>(),
             "Accept connections from outside default: yes if no -proxy or -connect(parameters: n, no, y, yes)")
            ("listenonion", bpo::value<string>(),
             "Automatically create Tor hidden service default: yes(parameters: n, no, y, yes)")
            ("maxconnections", bpo::value<unsigned int>(), "Maintain at most <n> connections to peers")
            ("maxreceivebuffer", bpo::value<size_t>(), "Maximum per-connection receive buffer, <n>*1000 bytes")
            ("maxsendbuffer", bpo::value<size_t>(), "Maximum per-connection send buffer, <n>*1000 bytes")
            ("maxtimeadjustment", bpo::value<int64_t>(),
             "Maximum allowed median peer time offset adjustment. Local perspective of time may be influenced by peers forward or backward by this amount.")
            ("onion", bpo::value<string>(),
             strprintf(_("Use separate SOCKS5 proxy to reach peers via Tor hidden services (default: %s)"),
                       "-proxy").c_str())
            ("onlynet", bpo::value<vector<string> >()->multitoken(),
             "Only connect to nodes in network <net> (ipv4, ipv6 or onion)")
            ("permitbaremultisig", bpo::value<string>(), "Relay non-P2SH multisig(parameters: n, no, y, yes)")
            ("peerbloomfilters", bpo::value<string>(),
             "Support filtering of blocks and transaction with bloom filters(parameters: n, no, y, yes)")
            ("port", bpo::value<int>(), strprintf(_("Listen for connections on <port> (default: %u or testnet: %u)"),
                                                  defaultChainParams->GetDefaultPort(),
                                                  testnetChainParams->GetDefaultPort()).c_str())
            ("proxy", bpo::value<string>(), "Connect through SOCKS5 proxy")
            ("proxyrandomize", bpo::value<string>(),
             "Randomize credentials for every proxy connection. This enables Tor stream isolation(parameters: n, no, y, yes)")
            ("seednode", bpo::value<vector<string> >()->multitoken(),
             "Connect to a node to retrieve peer addresses, and disconnect")
            ("timeout", bpo::value<int>(),
             strprintf(_("Specify connection timeout in milliseconds (minimum: 1, default: %d)"),
                       DEFAULT_CONNECT_TIMEOUT).c_str())
            ("torcontrol", bpo::value<string>(), "Tor control port to use if onion listening enabled")
            ("torpassword", bpo::value<string>(), "Tor control port password (default: empty)")

#ifdef USE_UPNP
#if USE_UPNP
    ("upnp", bpo::value<string>()->default_value("yes"), "Use UPnP to map the listening port default: yes when listening and no -proxy(parameters: n, no, y, yes)")
#else
    ("upnp", bpo::value<string>()->default_value("no"), "Use UPnP to map the listening port default: no when listening and no -proxy(parameters: n, no, y, yes)")
#endif
#endif

            ("whitebind", bpo::value<vector<string> >()->multitoken(),
             "Bind to given address and whitelist peers connecting to it. Use [host]:port notation for IPv6")
            ("whitelist", bpo::value<vector<string> >()->multitoken(),
             "Whitelist peers connecting from the given IP address (e.g. 1.2.3.4) or CIDR notated network (e.g. 1.2.3.0/24). Can be specified multiple times. "
                     "Whitelisted peers cannot be DoS banned and their transactions are always relayed, even if they are already in the mempool, useful e.g. for a gateway")
            ("maxuploadtarget", bpo::value<uint64_t>(), strprintf(
                    _("Tries to keep outbound traffic under the given target (in MiB per 24h), 0 = no limit (default: %d)"),
                    DEFAULT_MAX_UPLOAD_TARGET).c_str());
    app->add(connGroup);

    /******************************if ENABLE_WALLET begin***************************************/
#ifdef ENABLE_WALLET
    bpo::options_description walletGroup("Wallet options:");
    walletGroup.add_options()
            ("disablewallet", bpo::value<string>(),
             "Do not load the wallet and disable wallet RPC calls(parameters:: n, no, y, yes)")
            ("keypool", bpo::value<unsigned int>(), "Set key pool size")
            ("fallbackfee", bpo::value<string>(), strprintf(
                    _("A fee rate (in %s/kB) that will be used when fee estimation has insufficient data (default: %s)"),
                    CURRENCY_UNIT, FormatMoney(DEFAULT_FALLBACK_FEE)).c_str())
            ("discardfee", bpo::value<string>(), strprintf(
                    _("The fee rate (in %s/kB) that indicates your tolerance for discarding change by adding it to the fee (default: %s). "
                              "Note: An output is discarded if it is dust at this rate, but we will always discard up to the dust relay fee and a discard fee above that is limited by the fee estimate for the longest target"),
                    CURRENCY_UNIT, FormatMoney(DEFAULT_DISCARD_FEE)).c_str())
            ("mintxfee", bpo::value<string>(), strprintf(
                    _("Fees (in %s/kB) smaller than this are considered zero fee for transaction creation (default: %s)"),
                    CURRENCY_UNIT, FormatMoney(DEFAULT_TRANSACTION_MINFEE)).c_str())
            ("paytxfee", bpo::value<string>(),
             strprintf(_("Fee (in %s/kB) to add to transactions you send (default: %s)"),
                       CURRENCY_UNIT, FormatMoney(payTxFee.GetFeePerK())).c_str())
            ("rescan", bpo::value<string>(),
             "Rescan the block chain for missing wallet transactions on startup(parameters:: n, no, y, yes)")
            ("salvagewallet", bpo::value<string>(),
             "Attempt to recover private keys from a corrupt wallet on startup(parameters:: n, no, y, yes)")
            ("spendzeroconfchange", bpo::value<string>(),
             "Spend unconfirmed change when sending transactions(parameters:: n, no, y, yes)")
            ("txconfirmtarget", bpo::value<unsigned int>(),
             "If paytxfee is not set, include enough fee so transactions begin confirmation on average within n blocks")
            ("usehd", bpo::value<string>(),
             "Use hierarchical deterministic key generation (HD) after BIP32. Only has effect during wallet creation/first start(parameters:: n, no, y, yes)")
            ("walletrbf", bpo::value<string>(),
             "Send transactions with full-RBF opt-in enabled(parameters:: n, no, y, yes)")
            ("upgradewallet", bpo::value<int>(), "Upgrade wallet to latest format on startup(0 for false, 1 for true)")
            ("wallet", bpo::value<vector<string> >()->multitoken(), "Specify wallet file (within data directory)")
            ("walletbroadcast", bpo::value<string>(),
             "Make the wallet broadcast transactions(parameters:: n, no, y, yes)")
            ("walletnotify", bpo::value<string>(), "Execute command when a wallet transaction changes")
            ("zapwallettxes", bpo::value<int>(),
             "Delete all wallet transactions and only recover those parts of the blockchain through -rescan on startup"
                     "(1 = keep tx meta data e.g. account owner and payment request information, 2 = drop tx meta data)");
    app->add(walletGroup);
    /***********************************-help-debug begin****************************************************/
    bpo::options_description walletDebugGroup("Wallet debugging/testing options:");
    walletDebugGroup.add_options()
            ("dblogsize", bpo::value<unsigned int>(),
             "Flush wallet database activity from memory to disk log every <n> megabytes")
            ("flushwallet", bpo::value<string>(),
             "Run a thread to flush wallet periodically(parameters:: n, no, y, yes)")
            ("privdb", bpo::value<string>(),
             "Sets the DB_PRIVATE flag in the wallet db environment(parameters:: n, no, y, yes)")
            ("walletrejectlongchains", bpo::value<string>(),
             "Wallet will not create transactions that violate mempool chain limits(parameters:: n, no, y, yes)");
    app->add(walletDebugGroup);
    /***********************************-help-debug end******************************************************/
#endif
    /******************************if ENABLE_WALLET end*****************************************/

    /******************************if ENABLE_ZMQ begin***************************************/
#if ENABLE_ZMQ
    bpo::options_description zmqOptionGroup("ZeroMQ notification options:");
    zmqOptionGroup.add_options()
            ("zmqpubhashblock", bpo::value<string>(), "Enable publish hash block in <address>")
            ("zmqpubhashtx", bpo::value<string>(), "Enable publish hash transaction in <address>")
            ("zmqpubrawblock", bpo::value<string>(), "Enable publish raw block in <address>")
            ("zmqpubrawtx", bpo::value<string>(), "Enable publish raw transaction in <address>");
    app_bpo->add(zmqOptionGroup);
#endif
    /******************************if ENABLE_ZMQ end*****************************************/

    bpo::options_description debugGroup("Debugging/Testing options:");
    debugGroup.add_options()
            ("uacomment", bpo::value<vector<string> >()->multitoken(), "Append comment to the user agent string")
            /********************************-help-debug begin*********************************************/
            ("checkblocks", bpo::value<signed int>(),
             strprintf(_("How many blocks to check at startup (default: %u, 0 = all)"), DEFAULT_CHECKBLOCKS).c_str())
            ("checklevel", bpo::value<unsigned int>(),
             strprintf(_("How thorough the block verification of -checkblocks is (0-4, default: %u)"),
                       DEFAULT_CHECKLEVEL).c_str())
            ("checkblockindex", bpo::value<string>(), strprintf(
                    "Do a full consistency check for mapBlockIndex, setBlockIndexCandidates, chainActive and mapBlocksUnlinked occasionally. Also sets -checkmempool, default: %u(parameters:: n, no, y, yes)",
                    defaultChainParams->DefaultConsistencyChecks()).c_str())
            ("checkmempool", bpo::value<int>(), strprintf("Run checks every <n> transactions (default: %u)",
                                                          defaultChainParams->DefaultConsistencyChecks()).c_str())
            ("checkpoints", bpo::value<string>(),
             "Disable expensive verification for known chain history(parameters:: n, no, y, yes)")
            ("disablesafemode", bpo::value<string>(),
             "Disable safemode, override a real safe mode event(parameters:: n, no, y, yes)")
            ("testsafemode", bpo::value<string>(), "Force safe mode(parameters:: n, no, y, yes)")
            ("dropmessagestest", bpo::value<int>(), "Randomly drop 1 of every <n> network messages")
            ("fuzzmessagestest", bpo::value<int>(), "Randomly fuzz 1 of every <n> network messages")
            ("stopafterblockimport", bpo::value<string>(),
             "Stop running after importing blocks from disk(parameters:: n, no, y, yes)")
            ("stopatheight", bpo::value<int>(), "Stop running after reaching the given height in the main chain")
            ("limitancestorcount", bpo::value<unsigned int>(),
             "Do not accept transactions if number of in-mempool ancestors is <n> or more")
            ("limitancestorsize", bpo::value<unsigned int>(),
             "Do not accept transactions whose size with all in-mempool ancestors exceeds <n> kilobytes")
            ("limitdescendantcount", bpo::value<unsigned int>(),
             "Do not accept transactions if any ancestor would have <n> or more in-mempool descendants")
            ("limitdescendantsize", bpo::value<unsigned int>(),
             "Do not accept transactions if any ancestor would have more than <n> kilobytes of in-mempool descendants")
            ("vbparams", bpo::value<vector<string> >()->multitoken(),
             "Use given start/end times for specified version bits deployment (regtest-only)")
            /*********************************-help-debug end**********************************************/
            ("debug", bpo::value<vector<string> >()->multitoken(), (". " +
                                                                    _("If <category> is not supplied or if <category> = 1, output all debugging information.") +
                                                                    " " + _("<category> can be:") + " " +
                                                                    ListLogCategories() + ".").c_str())
            ("debugexclude", bpo::value<vector<string> >()->multitoken(),
             "Exclude debugging information for a category. Can be used in conjunction with -debug=1 to output debug logs for all categories except one or more specified categories.")
            ("-help-debug", bpo::value<string>(),
             "Show all debugging options, usage: --help -help-debug(parameters:: n, no, y, yes)")
            ("logips", bpo::value<string>(), "Include IP addresses in debug output(parameters:: n, no, y, yes)")
            ("logtimestamps", bpo::value<string>(), "Prepend debug output with timestamp(parameters:: n, no, y, yes)")
            /********************************-help-debug begin*********************************************/
            ("logtimemicros", bpo::value<string>(),
             "Add microsecond precision to debug timestamps(parameters:: n, no, y, yes)")
            ("mocktime", bpo::value<int32_t>(), "Replace actual time with <n> seconds since epoch (default: 0)")
            ("maxsigcachesize", bpo::value<unsigned int>(),
             "Limit sum of signature cache and script execution cache sizes to <n> MiB")
            ("maxtipage", bpo::value<int64_t>(),
             "Maximum tip age in seconds to consider node in initial block download")
            /*********************************-help-debug end**********************************************/
            ("maxtxfee", bpo::value<string>(), strprintf(
                    _("Maximum total fees (in %s) to use in a single wallet transaction or raw transaction; setting this too low may abort large transactions (default: %s)"),
                    CURRENCY_UNIT, FormatMoney(DEFAULT_TRANSACTION_MAXFEE)).c_str())
            ("printtoconsole", bpo::value<string>(),
             "Send trace/debug info to console instead of debug.log file(parameters:: n, no, y, yes)")
            ("printpriority", bpo::value<string>(),
             "Log transaction fee per kB when mining blocks(parameters:: n, no, y, yes)")  // -help-debug
            ("shrinkdebugfile", bpo::value<string>(),
             "Shrink debug.log file on client startup(parameters:: n, no, y, yes)");
    app->add(debugGroup);

    bpo::options_description chainSelectionGroup("Chain selection options:");
    chainSelectionGroup.add_options()
            ("testnet", "Use the test chain(parameters:: n, no, y, yes)")
            ("regtest",
             "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
                     "This is intended for regression testing tools and app_bpo development(parameters:: n, no, y, yes).");
    app->add(chainSelectionGroup);

    bpo::options_description nodeRelayGroup("Node relay options:");
    nodeRelayGroup.add_options()
            /********************************-help-debug begin*********************************************/
            ("acceptnonstdtxn", bpo::value<string>(),
             strprintf("Relay and mine \"non-standard\" transactions (%sdefault: %u)",
                       "testnet/regtest only(parameters:: n, no, y, yes).",
                       defaultChainParams->RequireStandard()).c_str())
            ("incrementalrelayfee", bpo::value<string>(), strprintf(
                    "Fee rate (in %s/kB) used to define cost of relay, used for mempool limiting and BIP 125 replacement. (default: %s)",
                    CURRENCY_UNIT, FormatMoney(DEFAULT_INCREMENTAL_RELAY_FEE)).c_str())
            ("dustrelayfee", bpo::value<string>(), strprintf(
                    "Fee rate (in %s/kB) used to defined dust, the value of an output such that it will cost more than its value in fees at this fee rate to spend it. (default: %s)",
                    CURRENCY_UNIT, FormatMoney(DUST_RELAY_TX_FEE)).c_str())
            /*********************************-help-debug end**********************************************/
            ("bytespersigop", bpo::value<unsigned int>(),
             "Equivalent bytes per sigop in transactions for relay and mining")
            ("datacarrier", bpo::value<string>(),
             "Relay and mine data carrier transactions(parameters:: n, no, y, yes).")
            ("datacarriersize", bpo::value<unsigned int>(),
             "Maximum size of data in data carrier transactions we relay and mine")
            ("mempoolreplacement", bpo::value<string>(),
             "Enable transaction replacement in the memory pool(parameters:: n, no, y, yes).")
            ("minrelaytxfee", bpo::value<string>(), strprintf(
                    _("Fees (in %s/kB) smaller than this are considered zero fee for relaying, mining and transaction creation (default: %s)"),
                    CURRENCY_UNIT, FormatMoney(DEFAULT_MIN_RELAY_TX_FEE)).c_str())
            ("whitelistrelay", bpo::value<string>(),
             "Accept relayed transactions received from whitelisted peers even when not relaying transactions(parameters:: n, no, y, yes)")
            ("whitelistforcerelay", bpo::value<string>(),
             "Force relay of transactions from whitelisted peers even if they violate local relay policy(parameters:: n, no, y, yes)");
    app->add(nodeRelayGroup);

    bpo::options_description blockCreateGroup("Block creation options:");
    blockCreateGroup.add_options()
            ("blockmaxweight", bpo::value<unsigned int>(), "Set maximum BIP141 block weight")
            ("blockmaxsize", bpo::value<unsigned int>(),
             "Set maximum BIP141 block weight to this * 4. Deprecated, use blockmaxweight")
            ("blockmintxfee", bpo::value<string>(), strprintf(
                    _("Set lowest fee rate (in %s/kB) for transactions to be included in block creation. (default: %s)"),
                    CURRENCY_UNIT, FormatMoney(DEFAULT_BLOCK_MIN_TX_FEE)).c_str())
            ("blockversion", bpo::value<int32_t>(),
             "Override block version to test forking scenarios");    // -help-debug
    app->add(blockCreateGroup);

    bpo::options_description rpcServerGroup("RPC server options:");
    rpcServerGroup.add_options()
            ("server", bpo::value<string>(), "Accept command line and JSON-RPC commands(choices: n, no, y, yes)")
            ("rest", bpo::value<string>(), "Accept public REST requests")
            ("rpcbind", bpo::value<vector<string> >()->multitoken(),
             _("Bind to given address to listen for JSON-RPC connections. This option is ignored unless -rpcallowip is also passed. Port is optional and overrides -rpcport. Use [host]:port notation for IPv6. This option can be specified multiple times (default: 127.0.0.1 and ::1 i.e., localhost, or if -rpcallowip has been specified, 0.0.0.0 and :: i.e., all addresses)").c_str())
            ("rpccookiefile", bpo::value<string>(), "Location of the auth cookie (default: data dir)")
            ("rpcuser", bpo::value<string>(), "Username for JSON-RPC connections")
            ("rpcpassword", bpo::value<string>(), "Password for JSON-RPC connections")
            ("rpcauth", bpo::value<vector<string> >()->multitoken(),
             _("Username and hashed password for JSON-RPC connections. The field <userpw> comes in the format: <USERNAME>:<SALT>$<HASH>. A canonical python script is included in share/rpcuser. The client then connects normally using the rpcuser=<USERNAME>/rpcpassword=<PASSWORD> pair of arguments. This option can be specified multiple times").c_str())
            ("rpcport", bpo::value<int>(),
             strprintf(_("Listen for JSON-RPC connections on <port> (default: %u or testnet: %u)"),
                       defaultBaseParams->RPCPort(), testnetBaseParams->RPCPort()).c_str())
            ("rpcallowip", bpo::value<vector<string> >()->multitoken(),
             _("Allow JSON-RPC connections from specified source. Valid for <ip> are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24). This option can be specified multiple times").c_str())
            ("rpcserialversion", bpo::value<unsigned int>(),
             "Sets the serialization of raw transaction or block hex returned in non-verbose mode, non-segwit(0) or segwit(1)")
            ("rpcthreads", bpo::value<int>(), "Set the number of threads to service RPC calls")
            /********************************-help-debug begin*********************************************/
            ("rpcworkqueue", bpo::value<int>(), "Set the depth of the work queue to service RPC calls")
            ("rpcservertimeout", bpo::value<int>(), "Timeout during HTTP requests");
    /*********************************-help-debug end**********************************************/
    app->add(rpcServerGroup);
    bpo::store(bpo::parse_command_line(argc, argv, *app), vm);
}

bool CArgsManager::Init(int argc, char *argv[])
{
    vector<string> argv_arr_tmp;
    vector<const char *> argv_arr;
    GenerateOptFormat(argc, (const char **)argv, argv_arr_tmp, argv_arr);
    bpo::options_description *app = new bpo::options_description("sbtcd");
    if (!PreProc(std::bind(&CArgsManager::InitPromOptions, this, std::placeholders::_1, std::placeholders::_2,
                           std::placeholders::_3, std::placeholders::_4, std::placeholders::_5), app, argv_arr.size(),
                 &argv_arr[0], HMM_BITCOIND))
    {
        return false;
    }
    return true;
}

/** Interpret string as boolean, for argument parsing */
bool CArgsManager::InterpretBool(const std::string &strValue)
{
    if (strValue.empty())
        return true;
    return (!strcmp(boost::to_lower_copy<std::string>(strValue).c_str(), "yes") ||
            !strcmp(boost::to_lower_copy<std::string>(strValue).c_str(), "y")
            || !strcmp(boost::to_lower_copy<std::string>(strValue).c_str(), "1") ||
            !strcmp(boost::to_lower_copy<std::string>(strValue).c_str(), "true")
           ) ? true : false;
}


bool CArgsManager::IsArgSet(const std::string &strArg) const
{
    LOCK(cs_args);
    std::string tmp_strArg = SubPrefix(strArg);

    return vm.count(tmp_strArg);
}


bool CArgsManager::merge_variable_map(bpo::variables_map &desc, bpo::variables_map &source)
{
    LOCK(cs_args);
    for (bpo::variables_map::iterator ite_src = source.begin(); ite_src != source.end(); ite_src++)
    {
        bpo::variables_map::iterator ite_desc = desc.find(ite_src->first);
        if (ite_desc != desc.end())  // find
        {
            vector<string>::const_iterator ite_options_arr = find(options_arr.begin(), options_arr.end(),
                                                                  ite_src->first);
            if (ite_options_arr != options_arr.end())    // value is array
            {
                vector<string> &desc_value = desc.at(ite_src->first).as<vector<string> >();
                desc_value.insert(desc_value.end(), ite_src->second.as<vector<string> >().begin(),
                                  ite_src->second.as<vector<string> >().end());

                // delete mutiple parameters
                sort(desc_value.begin(), desc_value.end());
                desc_value.erase(unique(desc_value.begin(), desc_value.end()), desc_value.end());
            } else
            {
                //value is basic data type, pass(use parameter from commond line)
            }
        } else    // not find
        {
            bpo::variable_value tmp_value = ite_src->second;
            auto res = desc.insert(std::make_pair(ite_src->first,
                                                  tmp_value)); // return type of insert is std::pair< map<string, bpo::variable_value>::iterator, bool >
            if (!res.second)
            {
                return false;
            }
        }
    }

    return true;
}

std::string CArgsManager::GetHelpMessage() const
{
    if (!app_bpo)
    {
        return std::string();
    }

    std::ostringstream oss;
    oss << *app_bpo << std::endl;
    return oss.str();
}

bool CArgsManager::PrintHelpMessage(std::function<void(void)> callback)
{
    if (vm.count("help"))
    {
        std::cout << *app_bpo << std::endl;
        return true;
    }

    if (vm.count("version"))
    {
        if (callback)
        {
            callback();
            return true;
        }
    }

    return false;
}

static fs::path pathCached;
static fs::path pathCachedNetSpecific;
static CCriticalSection csPathCached;

void CArgsManager::ClearDatadirCache() const
{
    LOCK(csPathCached);

    pathCached = fs::path();
    pathCachedNetSpecific = fs::path();
}

fs::path CArgsManager::GetConfigFile(const std::string &confPath) const
{
    fs::path pathConfigFile(confPath);
    if (!pathConfigFile.is_complete())
        pathConfigFile = GetDataDir(false) / pathConfigFile;

    return pathConfigFile;
}

void CArgsManager::ReadConfigFile(const std::string &confPath)
{
    bpo::variables_map vm_tmp;
    bfs::path config_file_name(GetConfigFile(confPath));
    bpo::store(bpo::parse_config_file<char>(config_file_name.make_preferred().string().c_str(), *app_bpo, true),
               vm_tmp);
    merge_variable_map(vm, vm_tmp);

    // If datadir is changed in .conf file:
    ClearDatadirCache();
}


const std::string CArgsManager::SubPrefix(std::string str) const
{
    std::string tmp_strArg;
    if (str[0] == '-')
    {
        tmp_strArg = str.substr(1);
    } else
    {
        tmp_strArg = str;
    }
    return tmp_strArg;
}

const fs::path &CArgsManager::GetDataDir(bool fNetSpecific) const
{

    LOCK(csPathCached);

    fs::path &path = fNetSpecific ? pathCachedNetSpecific : pathCached;

    // This can be called during exceptions by LogPrintf(), so we cache the
    // value so we don't have to do memory allocations after that.
    if (!path.empty())
        return path;

    if (IsArgSet("-datadir"))
    {
        std::string tmp = GetArg<std::string>("-datadir", "");
        path = fs::system_complete(GetArg<std::string>("-datadir", ""));
        if (!fs::is_directory(path))
        {
            path = "";
            return path;
        }
    } else
    {
        path = GetDefaultDataDir();
    }
    if (fNetSpecific)
        path /= app().GetBaseChainParams().DataDir();

    fs::create_directories(path);

    return path;
}
