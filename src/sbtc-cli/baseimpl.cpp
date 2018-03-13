#include <thread>
#include "baseimpl.hpp"
#include "utils/util.h"
#include "config/chainparams.h"
#include "sbtccore/clientversion.h"

CApp::CApp()
{
    nVersion = 1;
    bShutdown = false;
}

bool CApp::AppInitialize()
{
    SetupEnvironment();
    if (!SetupNetworking())
    {
        fprintf(stderr, "Error: Initializing networking failed\n");
        return EXIT_FAILURE;
    }
    return true;
}

void CApp::InitOptionMap()
{
    const auto defaultChainParams = CreateChainParams(CBaseChainParams::MAIN);
    const auto testnetChainParams = CreateChainParams(CBaseChainParams::TESTNET);

    std::map<string, vector<option_item>> optionMap;

    vector<option_item> item = {
            {"help,h",  "Print this message and exit."},
            {"help,h",  "Print this message and exit."},
            {"?",       "Print this message and exit."},
            {"version", "Print version and exit"},
            {"conf",    bpo::value<string>(),
                    strprintf(_("Specify configuration file (default: %s)"), BITCOIN_CONF_FILENAME).c_str()},
            {"datadir", bpo::value<string>(), "Specify data directory"}

    };
    optionMap.emplace("configuration options:", item);

    item = {
            {"testnet", bpo::value<string>(), "Use the test chain"},
            {"regtest", bpo::value<string>(),
                                              "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
                                                      "This is intended for regression testing tools and app_bpo development."},
            {"version", nullptr,              "Print version and exit"},
            {"conf",    bpo::value<string>(),
                    strprintf(_("Specify configuration file (default: %s)"), BITCOIN_CONF_FILENAME).c_str()},
            {"datadir", bpo::value<string>(), "Specify data directory"}

    };
    optionMap.emplace("Chain selection options:", item);

    item = {
            {"named",            bpo::value<string>(),
                    strprintf(_("Pass named instead of positional arguments (default: %s)"), DEFAULT_NAMED).c_str()},
            {"rpcconnect",       bpo::value<string>(),
                    strprintf(_("Send commands to node running on <ip> (default: %s)"), DEFAULT_RPCCONNECT).c_str()},
            {"rpcport",          bpo::value<int>(),
                    strprintf(_("Connect to JSON-RPC on <port> (default: %u or testnet: %u)"),
                              defaultChainParams->RPCPort(),
                              testnetChainParams->RPCPort()).c_str()},
            {"rpcwait",          bpo::value<string>(), "Wait for RPC server to start"},
            {"rpcuser",          bpo::value<string>(), "Username for JSON-RPC connections"},
            {"rpcpassword",      bpo::value<string>(), "Password for JSON-RPC connections"},
            {"rpcclienttimeout", bpo::value<int>(),
                    strprintf(_("Timeout in seconds during HTTP requests, or 0 for no timeout. (default: %d)"),
                              DEFAULT_HTTP_CLIENT_TIMEOUT).c_str()},
            {"stdin",            bpo::value<string>(),
                                                       "Read extra arguments from standard input, one per line until EOF/Ctrl-D (recommended for sensitive information such as passphrases)"},
            {"rpcwallet",        bpo::value<string>(),
                                                       "Send RPC for non-default wallet on RPC server (argument is wallet filename in bitcoind directory, required if bitcoind/-Qt runs with multiple wallets)"}
    };
    optionMap.emplace("rpc options:", item);

    std::string strHead =
            strprintf(_("%s RPC client version"), _(PACKAGE_NAME)) + " " + FormatFullVersion() + "\n" + "\n" +
            _("Usage:") + "\n" +
            "  bitcoin-cli [options] <command> [params]  " + strprintf(_("Send command to %s"), _(PACKAGE_NAME)) +
            "\n" +
            "  bitcoin-cli [options] -named <command> [name=value] ... " +
            strprintf(_("Send command to %s (with named arguments)"), _(PACKAGE_NAME)) + "\n" +
            "  bitcoin-cli [options] help                " + _("List commands") + "\n" +
            "  bitcoin-cli [options] help <command>      " + _("Get help for a command") + "\n";
    Args().SetOptionName(strHead);
    Args().SetOptionTable(optionMap);
}


