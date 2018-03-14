#include <thread>
#include <chrono>
#include <functional>
#include <signal.h>
#include <boost/interprocess/sync/file_lock.hpp>
#include <log4cpp/PropertyConfigurator.hh>
#include <log4cpp/PatternLayout.hh>
#include <log4cpp/RollingFileAppender.hh>
#include <log4cpp/OstreamAppender.hh>
#include "baseimpl.hpp"
#include "noui.h"
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
#include "wallet/wallet.h"

using namespace appbase;

CApp::CApp()
{
    nVersion = 1;
    bShutdown = false;
}

void CApp::InitOptionMap()
{
    const auto defaultChainParams = CreateChainParams(CChainParams::MAIN);
    const auto testnetChainParams = CreateChainParams(CChainParams::TESTNET);

    std::map<string, vector<option_item>> optionMap;

    vector<option_item> item = {
            {"help,h", "Print this help message and exit"},
            {"?", "Print this help message and exit"},
            {"version", "Print version and exit"},
            {"alertnotify", bpo::value<string>(),
             "Execute command when a relevant alert is received or we see a really long fork (%s in cmd is replaced by message)"
            },
            {"blocknotify", bpo::value<string>(),
             "Execute command when the best block changes (%s in cmd is replaced by block hash)"
            },
            {"blocksonly", bpo::value<string>(),
             "Whether to operate in a blocks only mode, default: no(parameters: n, no, y, yes)"
            },  // -help-debug
            {"assumevalid", bpo::value<string>(), strprintf(
                    _("If this block is in the chain assume that it and its ancestors are valid and potentially skip their script verification (0 to verify all, default: %s, testnet: %s)"),
                    defaultChainParams->GetConsensus().defaultAssumeValid.GetHex(),
                    testnetChainParams->GetConsensus().defaultAssumeValid.GetHex()).c_str()
            },
            {"conf", bpo::value<string>(), "Specify configuration file"},

            //if mode == HMM_BITCOIND
            //#if HAVE_DECL_DAEMON
            //            {"daemon", bpo::value<string>(),
            //             "Run in the background as a daemon and accept commands(parameters: n, no, y, yes)"} // dependence : mode, HAVE_DECL_DAEMON
            //#endif
            //#endif

            {"datadir", bpo::value<string>(), "Specify data directory"},
            {"dbbatchsize", bpo::value<int64_t>(), "Maximum database write batch size in bytes"},  // -help-debug
            {"dbcache", bpo::value<int64_t>(), "Set database cache size in megabytes"},
            {"feefilter", bpo::value<string>(),
             "Tell other nodes to filter invs to us by our mempool min fee (parameters: n, no, y, yes)"
            }, // -help-debug
            {"loadblock", bpo::value<vector<string> >()->multitoken(),
             "Imports blocks from external blk000??.dat file on startup"
            },
            {"maxorphantx", bpo::value<unsigned int>(), "Keep at most <n> unconnectable transactions in memory"},
            {"maxmempool", bpo::value<unsigned int>(), "Keep the transaction memory pool below <n> megabytes"},
            {
                    "mempoolexpiry", bpo::value<unsigned int>(),
                    "Do not keep transactions in the mempool longer than <n> hours"
            },
            {
                    "minimumchainwork", bpo::value<string>(),
                    "Minimum work assumed to exist on a valid chain in hex"
            },   // -help-debug
            {
                    "persistmempool", bpo::value<string>(),
                    "Whether to save the mempool on shutdown and load on restart (parameters: n, no, y, yes)"
            },
            {
                    "blockreconstructionextratxn", bpo::value<unsigned int>(),
                    "Extra transactions to keep in memory for compact block reconstructions"
            },
            {
                    "par", bpo::value<int>(), strprintf(
                    _("Set the number of script verification threads (%u to %d, 0 = auto, <0 = leave that many cores free, default: %d)"),
                    -GetNumCores(), MAX_SCRIPTCHECK_THREADS, DEFAULT_SCRIPTCHECK_THREADS).c_str()},

#ifndef WIN32
            {"pid", bpo::value<string>(), "Specify pid file"},
#endif

            {
                    "prune", bpo::value<int32_t>(), strprintf(
                    _("Reduce storage requirements by enabling pruning (deleting) of old blocks. This allows the pruneblockchain RPC to be called to delete specific blocks, and enables automatic pruning of old blocks if a target size in MiB is provided. This mode is incompatible with -txindex and -rescan. "
                              "Warning: Reverting this setting requires re-downloading the entire blockchain. "
                              "(default: 0 = disable pruning blocks, 1 = allow manual pruning via RPC, >%u = automatically prune block files to stay under the specified target size in MiB)"),
                    MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024).c_str()
            },
            {
                    "reindex-chainstate", bpo::value<string>(),
                    "Rebuild chain state from the currently indexed blocks (parameters: n, no, y, yes)"
            },
            {
                    "reindex", bpo::value<string>(),
                    "Rebuild chain state and block index from the blk*.dat files on disk(parameters: n, no, y, yes)"
            },

#ifndef WIN32
            {
                    "sysperms", bpo::value<string>(),
                    "Create new files with system default permissions, instead of umask 077, only effective with disabled wallet functionality (parameters: n, no, y, yes)"
            },
#endif

            {
                    "txindex", bpo::value<string>(),
                    "Maintain a full transaction index, used by the getrawtransaction rpc call(parameters: n, no, y, yes)"
            }

    };
    std::string strHead =
            strprintf(_("%s Daemon"), _(PACKAGE_NAME)) + " " + _("version") + " " + FormatFullVersion() + "\n" +
            "\n" + _("Usage:") + "\n" + "  bitcoind [options]                     " +
            strprintf(_("Start %s Daemon"), _(PACKAGE_NAME)) + "\n";
    optionMap.emplace(strHead, item);

    item = {{"addnode", bpo::value<vector<string> >()->multitoken(),
             "Add a node to connect to and attempt to keep the connection open"},
            {"banscore", bpo::value<unsigned int>(), "Threshold for disconnecting misbehaving peers"},
            {"bantime", bpo::value<unsigned int>(),
             "Number of seconds to keep misbehaving peers from reconnecting"},
            {"bind", bpo::value<vector<string> >()->multitoken(),
             "Bind to given address and always listen on it. Use [host]:port notation for IPv6"},
            {"connect", bpo::value<vector<string> >()->multitoken(),
             "Connect only to the specified node(s); -connect=0 disables automatic connections"},
            {"discover", bpo::value<string>(),
             "Discover own IP addresses default: yes when listening and no -externalip or -proxy(parameters: n, no, y, yes)"},
            {"dns", bpo::value<string>(),
             "Allow DNS lookups for -addnode, -seednode and -connect, default: yes(parameters: n, no, y, yes)"},
            {"dnsseed", bpo::value<string>(),
             "Query for peer addresses via DNS lookup, if low on addresses default: yes unless -connect used(parameters: n, no, y, yes)"},
            {"externalip", bpo::value<vector<string> >()->multitoken(), "Specify your own public address"},
            {"forcednsseed", bpo::value<string>(),
             "Always query for peer addresses via DNS lookup default: no(parameters: n, no, y, yes)"},
            {"listen", bpo::value<string>(),
             "Accept connections from outside default: yes if no -proxy or -connect(parameters: n, no, y, yes)"},
            {"listenonion", bpo::value<string>(),
             "Automatically create Tor hidden service default: yes(parameters: n, no, y, yes)"},
            {"maxconnections", bpo::value<unsigned int>(), "Maintain at most <n> connections to peers"},
            {"maxreceivebuffer", bpo::value<size_t>(), "Maximum per-connection receive buffer, <n>*1000 bytes"},
            {"maxsendbuffer", bpo::value<size_t>(), "Maximum per-connection send buffer, <n>*1000 bytes"},
            {"maxtimeadjustment", bpo::value<int64_t>(),
             "Maximum allowed median peer time offset adjustment. Local perspective of time may be influenced by peers forward or backward by this amount."},
            {"onion", bpo::value<string>(),
             strprintf(_("Use separate SOCKS5 proxy to reach peers via Tor hidden services (default: %s)"),
                       "-proxy").c_str()},
            {"onlynet", bpo::value<vector<string> >()->multitoken(),
             "Only connect to nodes in network <net> (ipv4, ipv6 or onion)"},
            {"permitbaremultisig", bpo::value<string>(), "Relay non-P2SH multisig(parameters: n, no, y, yes)"},
            {"peerbloomfilters", bpo::value<string>(),
             "Support filtering of blocks and transaction with bloom filters(parameters: n, no, y, yes)"},
            {"port", bpo::value<int>(),
             strprintf(_("Listen for connections on <port> (default: %u or testnet: %u)"),
                       defaultChainParams->GetDefaultPort(),
                       testnetChainParams->GetDefaultPort()).c_str()},
            {"proxy", bpo::value<string>(), "Connect through SOCKS5 proxy"},
            {"proxyrandomize", bpo::value<string>(),
             "Randomize credentials for every proxy connection. This enables Tor stream isolation(parameters: n, no, y, yes)"},
            {"seednode", bpo::value<vector<string> >()->multitoken(),
             "Connect to a node to retrieve peer addresses, and disconnect"},
            {"timeout", bpo::value<int>(),
             strprintf(_("Specify connection timeout in milliseconds (minimum: 1, default: %d)"),
                       DEFAULT_CONNECT_TIMEOUT).c_str()},
            {"torcontrol", bpo::value<string>(), "Tor control port to use if onion listening enabled"},
            {"torpassword", bpo::value<string>(), "Tor control port password (default: empty)"},

#ifdef USE_UPNP
#if USE_UPNP
    {"upnp", bpo::value<string>()->default_value("yes"), "Use UPnP to map the listening port default: yes when listening and no -proxy(parameters: n, no, y, yes)"},
#else
    {"upnp", bpo::value<string>()->default_value("no"), "Use UPnP to map the listening port default: no when listening and no -proxy(parameters: n, no, y, yes)"},
#endif
#endif

            {"whitebind", bpo::value<vector<string> >()->multitoken(),
             "Bind to given address and whitelist peers connecting to it. Use [host]:port notation for IPv6"},
            {"whitelist", bpo::value<vector<string> >()->multitoken(),
             "Whitelist peers connecting from the given IP address (e.g. 1.2.3.4) or CIDR notated network (e.g. 1.2.3.0/24). Can be specified multiple times. "
                     "Whitelisted peers cannot be DoS banned and their transactions are always relayed, even if they are already in the mempool, useful e.g. for a gateway"},
            {"maxuploadtarget", bpo::value<uint64_t>(), strprintf(
                    _("Tries to keep outbound traffic under the given target (in MiB per 24h), 0 = no limit (default: %d)"),
                    DEFAULT_MAX_UPLOAD_TARGET).c_str()}
    };
    optionMap.emplace("Connection options:", item);

    /******************************if ENABLE_WALLET begin***************************************/
#ifdef ENABLE_WALLET
    item = {
            {"disablewallet",       bpo::value<string>(),
                                                                "Do not load the wallet and disable wallet RPC calls(parameters:: n, no, y, yes)"},
            {"keypool",             bpo::value<unsigned int>(), "Set key pool size"},
            {"fallbackfee",         bpo::value<string>(), strprintf(
                    _("A fee rate (in %s/kB) that will be used when fee estimation has insufficient data (default: %s)"),
                    CURRENCY_UNIT, FormatMoney(DEFAULT_FALLBACK_FEE)).c_str()},
            {"discardfee",          bpo::value<string>(), strprintf(
                    _("The fee rate (in %s/kB) that indicates your tolerance for discarding change by adding it to the fee (default: %s). "
                              "Note: An output is discarded if it is dust at this rate, but we will always discard up to the dust relay fee and a discard fee above that is limited by the fee estimate for the longest target"),
                    CURRENCY_UNIT, FormatMoney(DEFAULT_DISCARD_FEE)).c_str()},
            {"mintxfee",            bpo::value<string>(), strprintf(
                    _("Fees (in %s/kB) smaller than this are considered zero fee for transaction creation (default: %s)"),
                    CURRENCY_UNIT, FormatMoney(DEFAULT_TRANSACTION_MINFEE)).c_str()},
            {"paytxfee",            bpo::value<string>(),
                                                          strprintf(
                                                                  _("Fee (in %s/kB) to add to transactions you send (default: %s)"),
                                                                  CURRENCY_UNIT,
                                                                  FormatMoney(payTxFee.GetFeePerK())).c_str()},
            {"rescan",              bpo::value<string>(),
                                                                "Rescan the block chain for missing wallet transactions on startup(parameters:: n, no, y, yes)"},
            {"salvagewallet",       bpo::value<string>(),
                                                                "Attempt to recover private keys from a corrupt wallet on startup(parameters:: n, no, y, yes)"},
            {"spendzeroconfchange", bpo::value<string>(),
                                                                "Spend unconfirmed change when sending transactions(parameters:: n, no, y, yes)"},
            {"txconfirmtarget",     bpo::value<unsigned int>(),
                                                                "If paytxfee is not set, include enough fee so transactions begin confirmation on average within n blocks"},
            {"usehd",               bpo::value<string>(),
                                                                "Use hierarchical deterministic key generation (HD) after BIP32. Only has effect during wallet creation/first start(parameters:: n, no, y, yes)"},
            {"walletrbf",           bpo::value<string>(),
                                                                "Send transactions with full-RBF opt-in enabled(parameters:: n, no, y, yes)"},
            {"upgradewallet",       bpo::value<int>(),
                                                                "Upgrade wallet to latest format on startup(0 for false, 1 for true)"},
            {"wallet",              bpo::value<vector<string> >()->multitoken(),
                                                                "Specify wallet file (within data directory)"},
            {"walletbroadcast",     bpo::value<string>(),
                                                                "Make the wallet broadcast transactions(parameters:: n, no, y, yes)"},
            {"walletnotify",        bpo::value<string>(),       "Execute command when a wallet transaction changes"},
            {"zapwallettxes",       bpo::value<int>(),
                                                                "Delete all wallet transactions and only recover those parts of the blockchain through -rescan on startup"
                                                                        "(1 = keep tx meta data e.g. account owner and payment request information, 2 = drop tx meta data)"}
    };
    optionMap.emplace("Wallet options:", item);

    /***********************************-help-debug begin****************************************************/
    item = {
            {"dblogsize",              bpo::value<unsigned int>(),
                    "Flush wallet database activity from memory to disk log every <n> megabytes"},
            {"flushwallet",            bpo::value<string>(),
                    "Run a thread to flush wallet periodically(parameters:: n, no, y, yes)"},
            {"privdb",                 bpo::value<string>(),
                    "Sets the DB_PRIVATE flag in the wallet db environment(parameters:: n, no, y, yes)"},
            {"walletrejectlongchains", bpo::value<string>(),
                    "Wallet will not create transactions that violate mempool chain limits(parameters:: n, no, y, yes)"}
    };
    optionMap.emplace("Wallet debugging/testing options:", item);
    /***********************************-help-debug end******************************************************/
#endif
    /******************************if ENABLE_WALLET end*****************************************/

    /******************************if ENABLE_ZMQ begin***************************************/
#if ENABLE_ZMQ
    item = {
            {"zmqpubhashblock", bpo::value<string>(), "Enable publish hash block in <address>"},
            {"zmqpubhashtx",    bpo::value<string>(), "Enable publish hash transaction in <address>"},
            {"zmqpubrawblock",  bpo::value<string>(), "Enable publish raw block in <address>"},
            {"zmqpubrawtx",     bpo::value<string>(), "Enable publish raw transaction in <address>"}
    };
    optionMap.emplace("ZeroMQ notification options:", item);
#endif
    /******************************if ENABLE_ZMQ end*****************************************/
    item = {
            {"uacomment",            bpo::value<vector<string> >()->multitoken(), "Append comment to the user agent string"},
            /********************************-help-debug begin*********************************************/
            {"checkblocks",          bpo::value<signed int>(),
                                                           strprintf(
                                                                   _("How many blocks to check at startup (default: %u, 0 = all)"),
                                                                   DEFAULT_CHECKBLOCKS).c_str()},
            {"checklevel",           bpo::value<unsigned int>(),
                                                           strprintf(
                                                                   _("How thorough the block verification of -checkblocks is (0-4, default: %u)"),
                                                                   DEFAULT_CHECKLEVEL).c_str()},
            {"checkblockindex",      bpo::value<string>(), strprintf(
                    "Do a full consistency check for mapBlockIndex, setBlockIndexCandidates, chainActive and mapBlocksUnlinked occasionally. Also sets -checkmempool, default: %u(parameters:: n, no, y, yes)",
                    defaultChainParams->DefaultConsistencyChecks()).c_str()},
            {"checkmempool",         bpo::value<int>(),    strprintf("Run checks every <n> transactions (default: %u)",
                                                                     defaultChainParams->DefaultConsistencyChecks()).c_str()},
            {"checkpoints",          bpo::value<string>(),
                                                                                  "Disable expensive verification for known chain history(parameters:: n, no, y, yes)"},
            {"disablesafemode",      bpo::value<string>(),
                                                                                  "Disable safemode, override a real safe mode event(parameters:: n, no, y, yes)"},
            {"testsafemode",         bpo::value<string>(),                        "Force safe mode(parameters:: n, no, y, yes)"},
            {"dropmessagestest",     bpo::value<int>(),                           "Randomly drop 1 of every <n> network messages"},
            {"fuzzmessagestest",     bpo::value<int>(),                           "Randomly fuzz 1 of every <n> network messages"},
            {"stopafterblockimport", bpo::value<string>(),
                                                                                  "Stop running after importing blocks from disk(parameters:: n, no, y, yes)"},
            {"stopatheight",         bpo::value<int>(),                           "Stop running after reaching the given height in the main chain"},
            {"limitancestorcount",   bpo::value<unsigned int>(),
                                                                                  "Do not accept transactions if number of in-mempool ancestors is <n> or more"},
            {"limitancestorsize",    bpo::value<unsigned int>(),
                                                                                  "Do not accept transactions whose size with all in-mempool ancestors exceeds <n> kilobytes"},
            {"limitdescendantcount", bpo::value<unsigned int>(),
                                                                                  "Do not accept transactions if any ancestor would have <n> or more in-mempool descendants"},
            {"limitdescendantsize",  bpo::value<unsigned int>(),
                                                                                  "Do not accept transactions if any ancestor would have more than <n> kilobytes of in-mempool descendants"},
            {"vbparams",             bpo::value<vector<string> >()->multitoken(),
                                                                                  "Use given start/end times for specified version bits deployment (regtest-only)"},
            /*********************************-help-debug end**********************************************/
            {"debug",                bpo::value<vector<string> >()->multitoken(), (". " +
                                                                                   _("If <category> is not supplied or if <category> = 1, output all debugging information.") +
                                                                                   " " + _("<category> can be:") + " " +
                                                                                   ListLogCategories() + ".").c_str()},
            {"debugexclude",         bpo::value<vector<string> >()->multitoken(),
                                                                                  "Exclude debugging information for a category. Can be used in conjunction with -debug=1 to output debug logs for all categories except one or more specified categories."},
            {"-help-debug",          bpo::value<string>(),
                                                                                  "Show all debugging options, usage: --help -help-debug(parameters:: n, no, y, yes)"},
            {"logips",               bpo::value<string>(),                        "Include IP addresses in debug output(parameters:: n, no, y, yes)"},
            {"logtimestamps",        bpo::value<string>(),                        "Prepend debug output with timestamp(parameters:: n, no, y, yes)"},
            /********************************-help-debug begin*********************************************/
            {"logtimemicros",        bpo::value<string>(),
                                                                                  "Add microsecond precision to debug timestamps(parameters:: n, no, y, yes)"},
            {"mocktime",             bpo::value<int32_t>(),                       "Replace actual time with <n> seconds since epoch (default: 0)"},
            {"maxsigcachesize",      bpo::value<unsigned int>(),
                                                                                  "Limit sum of signature cache and script execution cache sizes to <n> MiB"},
            {"maxtipage",            bpo::value<int64_t>(),
                                                                                  "Maximum tip age in seconds to consider node in initial block download"},
            /*********************************-help-debug end**********************************************/
            {"maxtxfee",             bpo::value<string>(), strprintf(
                    _("Maximum total fees (in %s) to use in a single wallet transaction or raw transaction; setting this too low may abort large transactions (default: %s)"),
                    CURRENCY_UNIT, FormatMoney(DEFAULT_TRANSACTION_MAXFEE)).c_str()},
            {"printtoconsole",       bpo::value<string>(),
                                                                                  "Send trace/debug info to console instead of debug.log file(parameters:: n, no, y, yes)"},
            {"printpriority",        bpo::value<string>(),
                                                                                  "Log transaction fee per kB when mining blocks(parameters:: n, no, y, yes)"},  // -help-debug
            {"shrinkdebugfile",      bpo::value<string>(),
                                                                                  "Shrink debug.log file on client startup(parameters:: n, no, y, yes)"}
    };
    optionMap.emplace("Debugging/Testing options:", item);

    item = {
            {"testnet", "Use the test chain(parameters:: n, no, y, yes)"},
            {"regtest",
                        "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
                                "This is intended for regression testing tools and app_bpo development(parameters:: n, no, y, yes)."}
    };
    optionMap.emplace("Chain selection options:", item);

    item = {
            {"acceptnonstdtxn",     bpo::value<string>(),
                                                          strprintf(
                                                                  "Relay and mine \"non-standard\" transactions (%sdefault: %u)",
                                                                  "testnet/regtest only(parameters:: n, no, y, yes).",
                                                                  defaultChainParams->RequireStandard()).c_str()},
            {"incrementalrelayfee", bpo::value<string>(), strprintf(
                    "Fee rate (in %s/kB) used to define cost of relay, used for mempool limiting and BIP 125 replacement. (default: %s)",
                    CURRENCY_UNIT, FormatMoney(DEFAULT_INCREMENTAL_RELAY_FEE)).c_str()},
            {"dustrelayfee",        bpo::value<string>(), strprintf(
                    "Fee rate (in %s/kB) used to defined dust, the value of an output such that it will cost more than its value in fees at this fee rate to spend it. (default: %s)",
                    CURRENCY_UNIT, FormatMoney(DUST_RELAY_TX_FEE)).c_str()},
            {"bytespersigop",       bpo::value<unsigned int>(),
                    "Equivalent bytes per sigop in transactions for relay and mining"},
            {"datacarrier",         bpo::value<string>(),
                    "Relay and mine data carrier transactions(parameters:: n, no, y, yes)."},
            {"datacarriersize",     bpo::value<unsigned int>(),
                    "Maximum size of data in data carrier transactions we relay and mine"},
            {"mempoolreplacement",  bpo::value<string>(),
                    "Enable transaction replacement in the memory pool(parameters:: n, no, y, yes)."},
            {"minrelaytxfee",       bpo::value<string>(), strprintf(
                    _("Fees (in %s/kB) smaller than this are considered zero fee for relaying, mining and transaction creation (default: %s)"),
                    CURRENCY_UNIT, FormatMoney(DEFAULT_MIN_RELAY_TX_FEE)).c_str()},
            {"whitelistrelay",      bpo::value<string>(),
                    "Accept relayed transactions received from whitelisted peers even when not relaying transactions(parameters:: n, no, y, yes)"},
            {"whitelistforcerelay", bpo::value<string>(),
                    "Force relay of transactions from whitelisted peers even if they violate local relay policy(parameters:: n, no, y, yes)"}
    };
    optionMap.emplace("Node relay options:", item);

    item = {
            {"blockmaxweight", bpo::value<unsigned int>(), "Set maximum BIP141 block weight"},
            {"blockmaxsize",   bpo::value<unsigned int>(),
                                                           "Set maximum BIP141 block weight to this * 4. Deprecated, use blockmaxweight"},
            {"blockmintxfee",  bpo::value<string>(), strprintf(
                    _("Set lowest fee rate (in %s/kB) for transactions to be included in block creation. (default: %s)"),
                    CURRENCY_UNIT, FormatMoney(DEFAULT_BLOCK_MIN_TX_FEE)).c_str()},
            {"blockversion",   bpo::value<int32_t>(),
                                                           "Override block version to test forking scenarios"}
    };
    optionMap.emplace("Block creation options:", item);

    item = {
            {"server",           bpo::value<string>(), "Accept command line and JSON-RPC commands(choices: n, no, y, yes)"},
            {"rest",             bpo::value<string>(), "Accept public REST requests"},
            {"rpcbind",          bpo::value<vector<string> >()->multitoken(),
                                                       _("Bind to given address to listen for JSON-RPC connections. This option is ignored unless -rpcallowip is also passed. Port is optional and overrides -rpcport. Use [host]:port notation for IPv6. This option can be specified multiple times (default: 127.0.0.1 and ::1 i.e., localhost, or if -rpcallowip has been specified, 0.0.0.0 and :: i.e., all addresses)").c_str()},
            {"rpccookiefile",    bpo::value<string>(), "Location of the auth cookie (default: data dir)"},
            {"rpcuser",          bpo::value<string>(), "Username for JSON-RPC connections"},
            {"rpcpassword",      bpo::value<string>(), "Password for JSON-RPC connections"},
            {"rpcauth",          bpo::value<vector<string> >()->multitoken(),
                                                       _("Username and hashed password for JSON-RPC connections. The field <userpw> comes in the format: <USERNAME>:<SALT>$<HASH>. A canonical python script is included in share/rpcuser. The client then connects normally using the rpcuser=<USERNAME>/rpcpassword=<PASSWORD> pair of arguments. This option can be specified multiple times").c_str()},
            {"rpcport",          bpo::value<int>(),
                    strprintf(_("Listen for JSON-RPC connections on <port> (default: %u or testnet: %u)"),
                              defaultChainParams->RPCPort(), testnetChainParams->RPCPort()).c_str()},
            {"rpcallowip",       bpo::value<vector<string> >()->multitoken(),
                                                       _("Allow JSON-RPC connections from specified source. Valid for <ip> are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24). This option can be specified multiple times").c_str()},
            {"rpcserialversion", bpo::value<unsigned int>(),
                                                       "Sets the serialization of raw transaction or block hex returned in non-verbose mode, non-segwit(0) or segwit(1)"},
            {"rpcthreads",       bpo::value<int>(),    "Set the number of threads to service RPC calls"},
            /********************************-help-debug begin*********************************************/
            {"rpcworkqueue",     bpo::value<int>(),    "Set the depth of the work queue to service RPC calls"},
            {"rpcservertimeout", bpo::value<int>(),    "Timeout during HTTP requests"}
    };
    optionMap.emplace("RPC server options:", item);

    pArgs->SetOptionName("sbtcd");
    pArgs->SetOptionTable(optionMap);
}

// Parameter interaction based on rules
void CApp::InitParameterInteraction()
{
    // when specifying an explicit binding address, you want to listen on it
    // even when -connect or -proxy is specified
    if (pArgs->IsArgSet("-bind"))
    {
        if (pArgs->SoftSetArg("-listen", true))
            mlog_notice("%s: parameter interaction: -bind set -> setting -listen=1\n", __func__);
    }
    if (pArgs->IsArgSet("-whitebind"))
    {
        if (pArgs->SoftSetArg("-listen", true))
            mlog_notice("%s: parameter interaction: -whitebind set -> setting -listen=1\n", __func__);
    }

    if (pArgs->IsArgSet("-connect"))
    {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (pArgs->SoftSetArg("-dnsseed", false))
            mlog_notice("%s: parameter interaction: -connect set -> setting -dnsseed=0\n", __func__);
        if (pArgs->SoftSetArg("-listen", false))
            mlog_notice("%s: parameter interaction: -connect set -> setting -listen=0\n", __func__);
    }

    if (pArgs->IsArgSet("-proxy"))
    {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (pArgs->SoftSetArg("-listen", false))
            mlog_notice("%s: parameter interaction: -proxy set -> setting -listen=0\n", __func__);
        // to protect privacy, do not use UPNP when a proxy is set. The user may still specify -listen=1
        // to listen locally, so don't rely on this happening through -listen below.
        if (pArgs->SoftSetArg("-upnp", false))
            mlog_notice("%s: parameter interaction: -proxy set -> setting -upnp=0\n", __func__);
        // to protect privacy, do not discover addresses by default
        if (pArgs->SoftSetArg("-discover", false))
            mlog_notice("%s: parameter interaction: -proxy set -> setting -discover=0\n", __func__);
    }

    if (!pArgs->GetArg<bool>("-listen", DEFAULT_LISTEN))
    {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        if (pArgs->SoftSetArg("-upnp", false))
            mlog_notice("%s: parameter interaction: -listen=0 -> setting -upnp=0\n", __func__);
        if (pArgs->SoftSetArg("-discover", false))
            mlog_notice("%s: parameter interaction: -listen=0 -> setting -discover=0\n", __func__);
        if (pArgs->SoftSetArg("-listenonion", false))
            mlog_notice("%s: parameter interaction: -listen=0 -> setting -listenonion=0\n", __func__);
    }

    if (pArgs->IsArgSet("-externalip"))
    {
        // if an explicit public IP is specified, do not try to find others
        if (pArgs->SoftSetArg("-discover", false))
            mlog_notice("%s: parameter interaction: -externalip set -> setting -discover=0\n", __func__);
    }

    // disable whitelistrelay in blocksonly mode
    if (pArgs->GetArg<bool>("-blocksonly", DEFAULT_BLOCKSONLY))
    {
        if (pArgs->SoftSetArg("-whitelistrelay", false))
            mlog_notice("%s: parameter interaction: -blocksonly=1 -> setting -whitelistrelay=0\n", __func__);
    }

    // Forcing relay from whitelisted hosts implies we will accept relays from them in the first place.
    if (pArgs->GetArg<bool>("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY))
    {
        if (pArgs->SoftSetArg("-whitelistrelay", true))
            mlog_notice("%s: parameter interaction: -whitelistforcerelay=1 -> setting -whitelistrelay=1\n",
                        __func__);
    }

    if (pArgs->IsArgSet("-blockmaxsize"))
    {
        unsigned int max_size = pArgs->GetArg<uint32_t>("-blockmaxsize", 0U);
        if (pArgs->SoftSetArg("-blockmaxweight", (uint32_t)(max_size * WITNESS_SCALE_FACTOR)))
        {
            mlog_notice(
                    "%s: parameter interaction: -blockmaxsize=%d -> setting -blockmaxweight=%d (-blockmaxsize is deprecated!)\n",
                    __func__, max_size, max_size * WITNESS_SCALE_FACTOR);
        } else
        {
            mlog_notice("%s: Ignoring blockmaxsize setting which is overridden by blockmaxweight", __func__);
        }
    }
}

static void HandleSIGTERM(int)
{
    GetApp()->RequestShutdown();
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
        mlog_error("Initializing networking failed");
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
    if (pArgs->GetArg<int32_t>("-prune", 0))
    {
        if (pArgs->GetArg<bool>("-txindex", DEFAULT_TXINDEX))
        {
            mlog_error("Prune mode is incompatible with -txindex.");
            return false;
        }
    }

    // -bind and -whitebind can't be set when not listening
    size_t nUserBind = pArgs->GetArgs("-bind").size() + pArgs->GetArgs("-whitebind").size();
    if (nUserBind != 0 && !pArgs->GetArg<bool>("listen", DEFAULT_LISTEN))
    {
        mlog_error("Cannot set -bind or -whitebind together with -listen=0");
        return false;
    }


    // Check for -debugnet
    if (pArgs->GetArg<bool>("-debugnet", false))
        mlog_warn("Unsupported argument -debugnet ignored, use -debug=net.");

    // Check for -socks - as this is a privacy risk to continue, exit here
    if (pArgs->IsArgSet("-socks"))
    {
        mlog_error(
                "Unsupported argument -socks found. Setting SOCKS version isn't possible anymore, only SOCKS5 proxies are supported.");
        return false;
    }

    // Check for -tor - as this is a privacy risk to continue, exit here
    if (pArgs->GetArg<bool>("-tor", false))
    {
        mlog_error("Unsupported argument -tor found, use -onion.");
        return false;
    }

    if (pArgs->GetArg<bool>("-benchmark", false))
        mlog_warn("Unsupported argument -benchmark ignored, use -debug=bench.");

    if (pArgs->GetArg<bool>("-whitelistalwaysrelay", false))
        mlog_warn(
                "Unsupported argument -whitelistalwaysrelay ignored, use -whitelistrelay and/or -whitelistforcerelay.");

    if (pArgs->IsArgSet("-blockminsize"))
        mlog_warn("Unsupported argument -blockminsize ignored.");

    //TODO: mempool component init.
    //    // Checkmempool and checkblockindex default to true in regtest mode
    //    int ratio = std::min<int>(
    //            std::max<int>(pArgs->GetArg<int32_t>("-checkmempool", chainparams.DefaultConsistencyChecks() ? 1 : 0), 0),
    //            1000000);
    //    if (ratio != 0)
    //    {
    //        mempool.setSanityCheck(1.0 / ratio);
    //    }

    //TODO: remove these temp variables.
    //    fCheckBlockIndex = pArgs->GetArg<bool>("-checkblockindex", chainparams.DefaultConsistencyChecks());
    //    fCheckpointsEnabled = pArgs->GetArg<bool>("-checkpoints", DEFAULT_CHECKPOINTS_ENABLED);

    uint256 hashAssumeValid = uint256S(
            pArgs->GetArg<std::string>("-assumevalid", chainparams.GetConsensus().defaultAssumeValid.GetHex()));
    if (!hashAssumeValid.IsNull())
        mlog_notice("Assuming ancestors of block %s have valid signatures.", hashAssumeValid.GetHex());
    else
        mlog_notice("Validating signatures for all blocks.");

    if (pArgs->IsArgSet("-minimumchainwork"))
    {
        const std::string minChainWorkStr = pArgs->GetArg<std::string>("-minimumchainwork", "");
        if (!IsHexNumber(minChainWorkStr))
        {
            mlog_error("Invalid non-hex (%s) minimum chain work value specified", minChainWorkStr);
            return false;
        }
        nMinimumChainWork = UintToArith256(uint256S(minChainWorkStr));
    } else
    {
        nMinimumChainWork = UintToArith256(chainparams.GetConsensus().nMinimumChainWork);
    }
    mlog_notice("Setting nMinimumChainWork=%s.", nMinimumChainWork.GetHex());
    if (nMinimumChainWork < UintToArith256(chainparams.GetConsensus().nMinimumChainWork))
    {
        mlog_warn("Warning: nMinimumChainWork set below default value of %s.",
                  chainparams.GetConsensus().nMinimumChainWork.GetHex());
    }

    // mempool limits
    int64_t nMempoolSizeMax = pArgs->GetArg<uint32_t>("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    int64_t nMempoolSizeMin =
            pArgs->GetArg<uint32_t>("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000 * 40;
    if (nMempoolSizeMax < 0 || nMempoolSizeMax < nMempoolSizeMin)
    {
        mlog_error("-maxmempool must be at least %d MB", std::ceil(nMempoolSizeMin / 1000000.0));
        return false;
    }

    // incremental relay fee sets the minimum feerate increase necessary for BIP 125 replacement in the mempool
    // and the amount the mempool min fee increases above the feerate of txs evicted due to mempool limiting.
    if (pArgs->IsArgSet("-incrementalrelayfee"))
    {
        CAmount n = 0;
        if (!ParseMoney(pArgs->GetArg<std::string>("-incrementalrelayfee", ""), n))
        {
            mlog_error(
                    AmountErrMsg("incrementalrelayfee", pArgs->GetArg<std::string>("-incrementalrelayfee", "")));
            return false;
        }

        incrementalRelayFee = CFeeRate(n);
    }

    // block pruning; get the amount of disk space (in MiB) to allot for block & undo files
    int64_t nPruneArg = pArgs->GetArg<int32_t>("-prune", 0);
    if (nPruneArg < 0)
    {
        mlog_error("Prune cannot be configured with a negative value.");
        return false;
    }
    nPruneTarget = (uint64_t)nPruneArg * 1024 * 1024;
    if (nPruneArg == 1)
    {  // manual pruning: -prune=1
        mlog_notice(
                "Block pruning enabled.  Use RPC call pruneblockchain(height) to manually prune block and undo files.");
        nPruneTarget = std::numeric_limits<uint64_t>::max();
        fPruneMode = true;
    } else if (nPruneTarget)
    {
        if (nPruneTarget < MIN_DISK_SPACE_FOR_BLOCK_FILES)
        {
            mlog_error("Prune configured below the minimum of %d MiB.  Please use a higher number.",
                       MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024);
            return false;
        }
        mlog_notice("Prune configured to target %uMiB on disk for block and undo files.",
                    nPruneTarget / 1024 / 1024);
        fPruneMode = true;
    }

    nConnectTimeout = pArgs->GetArg<int32_t>("-timeout", DEFAULT_CONNECT_TIMEOUT);
    if (nConnectTimeout <= 0)
        nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;

    if (pArgs->IsArgSet("-minrelaytxfee"))
    {
        CAmount n = 0;
        if (!ParseMoney(pArgs->GetArg<std::string>("-minrelaytxfee", ""), n))
        {
            mlog_error(AmountErrMsg("minrelaytxfee", pArgs->GetArg<std::string>("-minrelaytxfee", "")));
            return false;
        }
        // High fee check is done afterward in CWallet::ParameterInteraction()
        ::minRelayTxFee = CFeeRate(n);
    } else if (incrementalRelayFee > ::minRelayTxFee)
    {
        // Allow only setting incrementalRelayFee to control both
        ::minRelayTxFee = incrementalRelayFee;
        mlog_notice("Increasing minrelaytxfee to %s to match incrementalrelayfee.", ::minRelayTxFee.ToString());
    }

    // Sanity check argument for min fee for including tx in block
    // TODO: Harmonize which arguments need sanity checking and where that happens
    if (pArgs->IsArgSet("-blockmintxfee"))
    {
        CAmount n = 0;
        if (!ParseMoney(pArgs->GetArg<std::string>("-blockmintxfee", ""), n))
        {
            mlog_error(AmountErrMsg("blockmintxfee", pArgs->GetArg<std::string>("-blockmintxfee", "")));
            return false;
        }
    }

    // Feerate used to define dust.  Shouldn't be changed lightly as old
    // implementations may inadvertently create non-standard transactions
    if (pArgs->IsArgSet("-dustrelayfee"))
    {
        CAmount n = 0;
        if (!ParseMoney(pArgs->GetArg<std::string>("-dustrelayfee", ""), n) || 0 == n)
        {
            mlog_error(AmountErrMsg("dustrelayfee", pArgs->GetArg<std::string>("-dustrelayfee", "")));
            return false;
        }
        dustRelayFee = CFeeRate(n);
    }

    fRequireStandard = !pArgs->GetArg<bool>("-acceptnonstdtxn", !chainparams.RequireStandard());
    if (chainparams.RequireStandard() && !fRequireStandard)
    {
        mlog_error(
                "acceptnonstdtxn is not currently supported for %s chain", chainparams.NetworkIDString());
        return false;
    }

    nBytesPerSigOp = pArgs->GetArg<uint32_t>("-bytespersigop", nBytesPerSigOp);

    //#ifdef ENABLE_WALLET
    //    if (!CWallet::ParameterInteraction())
    //        return false;
    //#endif

    fIsBareMultisigStd = pArgs->GetArg<bool>("-permitbaremultisig", DEFAULT_PERMIT_BAREMULTISIG);
    fAcceptDatacarrier = pArgs->GetArg<bool>("-datacarrier", DEFAULT_ACCEPT_DATACARRIER);
    nMaxDatacarrierBytes = pArgs->GetArg<uint32_t>("-datacarriersize", nMaxDatacarrierBytes);

    // Option to startup with mocktime set (used for regression testing):
    SetMockTime(pArgs->GetArg<int32_t>("-mocktime", 0)); // SetMockTime(0) is a no-op

    if (pArgs->GetArg<bool>("-peerbloomfilters", DEFAULT_PEERBLOOMFILTERS))
        nLocalServices = ServiceFlags(nLocalServices | NODE_BLOOM);

    if (pArgs->GetArg<uint32_t>("-rpcserialversion", DEFAULT_RPC_SERIALIZE_VERSION) < 0)
    {
        mlog_error("rpcserialversion must be non-negative.");
        return false;
    }

    if (pArgs->GetArg<uint32_t>("-rpcserialversion", DEFAULT_RPC_SERIALIZE_VERSION) > 1)
    {
        mlog_error("unknown rpcserialversion requested.");
        return false;
    }

    nMaxTipAge = pArgs->GetArg<int64_t>("maxtipage", DEFAULT_MAX_TIP_AGE);

    fEnableReplacement = pArgs->GetArg<bool>("-mempoolreplacement", DEFAULT_ENABLE_REPLACEMENT);

    if (pArgs->IsArgSet("-vbparams"))
    {
        // Allow overriding version bits parameters for testing
        if (!chainparams.MineBlocksOnDemand())
        {
            mlog_error("Version bits parameters may only be overridden on regtest.");
            return false;
        }
        for (const std::string &strDeployment : pArgs->GetArgs("-vbparams"))
        {
            std::vector<std::string> vDeploymentParams;
            boost::split(vDeploymentParams, strDeployment, boost::is_any_of(":"));
            if (vDeploymentParams.size() != 3)
            {
                mlog_error("Version bits parameters malformed, expecting deployment:start:end");
                return false;
            }
            int64_t nStartTime, nTimeout;
            if (!ParseInt64(vDeploymentParams[1], &nStartTime))
            {
                mlog_error("Invalid nStartTime (%s)", vDeploymentParams[1]);
                return false;
            }
            if (!ParseInt64(vDeploymentParams[2], &nTimeout))
            {
                mlog_error("Invalid nTimeout (%s)", vDeploymentParams[2]);
                return false;
            }
            bool found = false;
            for (int j = 0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j)
            {
                if (vDeploymentParams[0].compare(VersionBitsDeploymentInfo[j].name) == 0)
                {
                    pChainParams->UpdateVersionBitsParameters(Consensus::DeploymentPos(j), nStartTime, nTimeout);
                    found = true;
                    mlog_notice("Setting version bits activation parameters for %s to start=%ld, timeout=%ld\n",
                                vDeploymentParams[0], nStartTime, nTimeout);
                    break;
                }
            }
            if (!found)
            {
                mlog_error("Invalid deployment (%s)", vDeploymentParams[0]);
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
    mlog_notice("Using the '%s' SHA256 implementation.", sha256_algo);
    RandomInit();
    ECC_Start();
    globalVerifyHandle.reset(new ECCVerifyHandle());

    // Sanity check
    if (!InitSanityCheck())
    {
        mlog_error("Initialization sanity check failed. %s is shutting down.", PACKAGE_NAME);
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

bool CApp::AppInitialize()
{
    noui_connect();

    // -server defaults to true for bitcoind but not for the GUI so do this here
    pArgs->SoftSetArg("-server", true);

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
    if (pArgs->GetArg<bool>("-daemon", false))
    {
#if HAVE_DECL_DAEMON
        mlog_notice("sbtcd server starting.");

        // Daemonize
        if (daemon(1, 0))
        { // don't chdir (1), do close FDs (0)
            mlog_error("Error: daemon() failed: %s.", strerror(errno));
            return false;
        }
#else
        mlog_error("Error: -daemon is not supported on this operating system.");
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

#ifndef WIN32

    CreatePidFile(GetPidFile(), getpid());
#endif

    mlog_notice("Default data directory %s.", GetDefaultDataDir().string());
    mlog_notice("Using data directory %s.", GetDataDir().string());
    mlog_notice("Using config file %s.",
                GetConfigFile(pArgs->GetArg<std::string>("conf", std::string(BITCOIN_CONF_FILENAME))).string());

    return true;
}

bool CApp::Shutdown()
{
    if (scheduler)
    {
        scheduler->stop();
        if (schedulerThread.joinable())
        {
            schedulerThread.join();
        }
    }

    if (eventManager)
    {
        eventManager->Uninit(true);
    }

    bool fRet = IBaseApp::Shutdown();

#ifndef WIN32
    try
    {
        fs::remove(GetPidFile());
    } catch (const fs::filesystem_error &e)
    {
        mlog_warn("%s: Unable to remove pidfile: %s.", __func__, e.what());
    }
#endif
    UnregisterAllValidationInterfaces();
    GetMainSignals().UnregisterBackgroundSignalScheduler();
    m_mapComponents.clear();
    globalVerifyHandle.reset();
    ECC_Stop();
    mlog_notice("%s: done.", __func__);
    return fRet;
}


