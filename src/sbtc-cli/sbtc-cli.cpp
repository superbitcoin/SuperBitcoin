// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)

#include "config/sbtc-config.h"

#endif

#include "config/chainparamsbase.h"
#include "sbtccore/clientversion.h"
#include "fs.h"
#include "rpc/client.h"
#include "rpc/protocol.h"
#include "utils/util.h"
#include "utils/utilstrencodings.h"

#include <stdio.h>

#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>
#include "utils/net/events.h"

#include <univalue.h>

#include <iostream>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>

namespace bpo = boost::program_options;
using std::string;

static const char DEFAULT_RPCCONNECT[] = "127.0.0.1";
static const int DEFAULT_HTTP_CLIENT_TIMEOUT = 900;
static const bool DEFAULT_NAMED = false;
static const int CONTINUE_EXECUTION = -1;

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
//
// Exception thrown on connection error.  This error is used to determine
// when to wait if -rpcwait is given.
//
void InitPromOptions(bpo::options_description *app, bpo::variables_map &vm, int argc, const char **argv,
                     HelpMessageMode mode)
{
    const auto defaultBaseParams = CreateBaseChainParams(CBaseChainParams::MAIN);
    const auto testnetBaseParams = CreateBaseChainParams(CBaseChainParams::TESTNET);

    bpo::options_description confGroup("configuration options:");
    confGroup.add_options()
            ("help,h", "Print this message and exit.")
            ("?", "Print this message and exit.")
            ("version", "Print version and exit")
            ("conf", bpo::value<string>(),
             strprintf(_("Specify configuration file (default: %s)"), BITCOIN_CONF_FILENAME).c_str())
            ("datadir", bpo::value<string>(), "Specify data directory");
    app->add(confGroup);

    bpo::options_description chainGroup("Chain selection options:");
    chainGroup.add_options()
            ("testnet", bpo::value<string>(), "Use the test chain")
            ("regtest", bpo::value<string>(),
             "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
                     "This is intended for regression testing tools and app_bpo development.");
    app->add(chainGroup);

    bpo::options_description rpcGroup("rpc options:");
    rpcGroup.add_options()
            ("named", bpo::value<string>(),
             strprintf(_("Pass named instead of positional arguments (default: %s)"), DEFAULT_NAMED).c_str())
            ("rpcconnect", bpo::value<string>(),
             strprintf(_("Send commands to node running on <ip> (default: %s)"), DEFAULT_RPCCONNECT).c_str())
            ("rpcport", bpo::value<int>(),
             strprintf(_("Connect to JSON-RPC on <port> (default: %u or testnet: %u)"), defaultBaseParams->RPCPort(),
                       testnetBaseParams->RPCPort()).c_str())
            ("rpcwait", bpo::value<string>(), "Wait for RPC server to start")
            ("rpcuser", bpo::value<string>(), "Username for JSON-RPC connections")
            ("rpcpassword", bpo::value<string>(), "Password for JSON-RPC connections")
            ("rpcclienttimeout", bpo::value<int>(),
             strprintf(_("Timeout in seconds during HTTP requests, or 0 for no timeout. (default: %d)"),
                       DEFAULT_HTTP_CLIENT_TIMEOUT).c_str())
            ("stdin", bpo::value<string>(),
             "Read extra arguments from standard input, one per line until EOF/Ctrl-D (recommended for sensitive information such as passphrases)")
            ("rpcwallet", bpo::value<string>(),
             "Send RPC for non-default wallet on RPC server (argument is wallet filename in bitcoind directory, required if bitcoind/-Qt runs with multiple wallets)");
    app->add(rpcGroup);

    bpo::store(bpo::parse_command_line(argc, argv, *app), vm);
}

class CConnectionFailed : public std::runtime_error
{
public:

    explicit inline CConnectionFailed(const std::string &msg) :
            std::runtime_error(msg)
    {
    }

};

//
// This function returns either one of EXIT_ codes when it's expected to stop the process or
// CONTINUE_EXECUTION when it's expected to continue further.
//
void PrintVersion()
{
    std::cout << strprintf(_("%s RPC client version"), _(PACKAGE_NAME)) + " " + FormatFullVersion() + "\n" << std::endl;
}

static int AppInitRPC(int argc, char *argv[])
{
    //
    // Parameters
    //
    if (argc < 2)
    {
        fprintf(stdout, "%s", "Error: too few parameters, please enter: sbtc-cli --help for help.\n");

        return EXIT_FAILURE;
    }

    std::string strHead =
            strprintf(_("%s RPC client version"), _(PACKAGE_NAME)) + " " + FormatFullVersion() + "\n" + "\n" +
            _("Usage:") + "\n" +
            "  bitcoin-cli [options] <command> [params]  " + strprintf(_("Send command to %s"), _(PACKAGE_NAME)) +
            "\n" +
            "  bitcoin-cli [options] -named <command> [name=value] ... " +
            strprintf(_("Send command to %s (with named arguments)"), _(PACKAGE_NAME)) + "\n" +
            "  bitcoin-cli [options] help                " + _("List commands") + "\n" +
            "  bitcoin-cli [options] help <command>      " + _("Get help for a command") + "\n";

    vector<string> argv_arr_tmp;
    vector<const char *> argv_arr;
    GenerateOptFormat(argc, (const char **)argv, argv_arr_tmp, argv_arr);
    bpo::options_description *app = new bpo::options_description(strHead.c_str());
    if (!gArgs.PreProc(InitPromOptions, app, argv_arr.size(), &argv_arr[0], HMM_EMPTY))
    {
        return EXIT_FAILURE;
    }

    if (gArgs.PrintHelpMessage(PrintVersion))
    {
        return EXIT_SUCCESS;
    }

    if (!fs::is_directory(GetDataDir(false)))
    {
        fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n",
                gArgs.GetArg<std::string>("-datadir", "").c_str());
        return EXIT_FAILURE;
    }
    try
    {
        gArgs.ReadConfigFile(gArgs.GetArg<std::string>("-conf", BITCOIN_CONF_FILENAME));
    } catch (const std::exception &e)
    {
        fprintf(stderr, "Error reading configuration file: %s\n", e.what());
        return EXIT_FAILURE;
    }
    // Check for -testnet or -regtest parameter (BaseParams() calls are only valid after this clause)
    try
    {
        SelectBaseParams(ChainNameFromCommandLine());
    } catch (const std::exception &e)
    {
        fprintf(stderr, "Error: %s\n", e.what());
        return EXIT_FAILURE;
    }
    if (gArgs.GetArg<bool>("-rpcssl", false))
    {
        fprintf(stderr, "Error: SSL mode for RPC (-rpcssl) is no longer supported.\n");
        return EXIT_FAILURE;
    }
    return CONTINUE_EXECUTION;
}


/** Reply structure for request_done to fill in */
struct HTTPReply
{
    HTTPReply() : status(0), error(-1)
    {
    }

    int status;
    int error;
    std::string body;
};

const char *http_errorstring(int code)
{
    switch (code)
    {
#if LIBEVENT_VERSION_NUMBER >= 0x02010300
        case EVREQ_HTTP_TIMEOUT:
            return "timeout reached";
        case EVREQ_HTTP_EOF:
            return "EOF reached";
        case EVREQ_HTTP_INVALID_HEADER:
            return "error while reading header, or invalid header";
        case EVREQ_HTTP_BUFFER_ERROR:
            return "error encountered while reading or writing";
        case EVREQ_HTTP_REQUEST_CANCEL:
            return "request was canceled";
        case EVREQ_HTTP_DATA_TOO_LONG:
            return "response body is larger than allowed";
#endif
        default:
            return "unknown";
    }
}

static void http_request_done(struct evhttp_request *req, void *ctx)
{
    HTTPReply *reply = static_cast<HTTPReply *>(ctx);

    if (req == nullptr)
    {
        /* If req is nullptr, it means an error occurred while connecting: the
         * error code will have been passed to http_error_cb.
         */
        reply->status = 0;
        return;
    }

    reply->status = evhttp_request_get_response_code(req);

    struct evbuffer *buf = evhttp_request_get_input_buffer(req);
    if (buf)
    {
        size_t size = evbuffer_get_length(buf);
        const char *data = (const char *)evbuffer_pullup(buf, size);
        if (data)
            reply->body = std::string(data, size);
        evbuffer_drain(buf, size);
    }
}

#if LIBEVENT_VERSION_NUMBER >= 0x02010300

static void http_error_cb(enum evhttp_request_error err, void *ctx)
{
    HTTPReply *reply = static_cast<HTTPReply *>(ctx);
    reply->error = err;
}

#endif

UniValue CallRPC(const std::string &strMethod, const UniValue &params)
{
    std::string host;
    // In preference order, we choose the following for the port:
    //     1. -rpcport
    //     2. port in -rpcconnect (ie following : in ipv4 or ]: in ipv6)
    //     3. default port for chain
    int port = BaseParams().RPCPort();
    SplitHostPort(gArgs.GetArg<std::string>("-rpcconnect", DEFAULT_RPCCONNECT), port, host);
    port = gArgs.GetArg<int>("-rpcport", port);

    // Obtain event base
    raii_event_base base = obtain_event_base();

    // Synchronously look up hostname
    raii_evhttp_connection evcon = obtain_evhttp_connection_base(base.get(), host, port);
    evhttp_connection_set_timeout(evcon.get(), gArgs.GetArg<int>("-rpcclienttimeout", DEFAULT_HTTP_CLIENT_TIMEOUT));

    HTTPReply response;
    raii_evhttp_request req = obtain_evhttp_request(http_request_done, (void *)&response);
    if (req == nullptr)
        throw std::runtime_error("create http request failed");
#if LIBEVENT_VERSION_NUMBER >= 0x02010300
    evhttp_request_set_error_cb(req.get(), http_error_cb);
#endif

    // Get credentials
    std::string strRPCUserColonPass;
    if (gArgs.GetArg<std::string>("-rpcpassword", "") == "")
    {
        // Try fall back to cookie-based authentication if no password is provided
        if (!GetAuthCookie(&strRPCUserColonPass))
        {
            throw std::runtime_error(strprintf(
                    _("Could not locate RPC credentials. No authentication cookie could be found, and no rpcpassword is set in the configuration file (%s)"),
                    GetConfigFile(
                            gArgs.GetArg<std::string>("-conf", std::string(BITCOIN_CONF_FILENAME))).string().c_str()));

        }
    } else
    {
        strRPCUserColonPass =
                gArgs.GetArg<std::string>("-rpcuser", "") + ":" + gArgs.GetArg<std::string>("-rpcpassword", "");
    }

    struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req.get());
    assert(output_headers);
    evhttp_add_header(output_headers, "Host", host.c_str());
    evhttp_add_header(output_headers, "Connection", "close");
    evhttp_add_header(output_headers, "Authorization",
                      (std::string("Basic ") + EncodeBase64(strRPCUserColonPass)).c_str());

    // Attach request data
    std::string strRequest = JSONRPCRequestObj(strMethod, params, 1).write() + "\n";
    struct evbuffer *output_buffer = evhttp_request_get_output_buffer(req.get());
    assert(output_buffer);
    evbuffer_add(output_buffer, strRequest.data(), strRequest.size());

    // check if we should use a special wallet endpoint
    std::string endpoint = "/";
    std::string walletName = gArgs.GetArg<std::string>("-rpcwallet", "");
    if (!walletName.empty())
    {
        char *encodedURI = evhttp_uriencode(walletName.c_str(), walletName.size(), false);
        if (encodedURI)
        {
            endpoint = "/wallet/" + std::string(encodedURI);
            free(encodedURI);
        } else
        {
            throw CConnectionFailed("uri-encode failed");
        }
    }
    int r = evhttp_make_request(evcon.get(), req.get(), EVHTTP_REQ_POST, endpoint.c_str());
    req.release(); // ownership moved to evcon in above call
    if (r != 0)
    {
        throw CConnectionFailed("send http request failed");
    }

    event_base_dispatch(base.get());

    if (response.status == 0)
        throw CConnectionFailed(strprintf(
                "couldn't connect to server: %s (code %d)\n(make sure server is running and you are connecting to the correct RPC port)",
                http_errorstring(response.error), response.error));
    else if (response.status == HTTP_UNAUTHORIZED)
        throw std::runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
    else if (response.status >= 400 && response.status != HTTP_BAD_REQUEST && response.status != HTTP_NOT_FOUND &&
             response.status != HTTP_INTERNAL_SERVER_ERROR)
        throw std::runtime_error(strprintf("server returned HTTP error %d", response.status));
    else if (response.body.empty())
        throw std::runtime_error("no response from server");

    // Parse reply
    UniValue valReply(UniValue::VSTR);
    if (!valReply.read(response.body))
        throw std::runtime_error("couldn't parse reply from server");
    const UniValue &reply = valReply.get_obj();
    if (reply.empty())
        throw std::runtime_error("expected reply to have result, error and id properties");

    return reply;
}

int CommandLineRPC(int argc, char *argv[])
{
    std::string strPrint;
    int nRet = 0;
    try
    {
        // Skip switches
        while (argc > 1 && IsSwitchChar(argv[1][0]))
        {
            argc--;
            argv++;
        }
        std::vector<std::string> args = std::vector<std::string>(&argv[1], &argv[argc]);
        if (gArgs.GetArg<bool>("-stdin", false))
        {
            // Read one arg per line from stdin and append
            std::string line;
            while (std::getline(std::cin, line))
                args.push_back(line);
        }
        if (args.size() < 1)
            throw std::runtime_error("too few parameters (need at least command)");
        std::string strMethod = args[0];
        args.erase(args.begin()); // Remove trailing method name from arguments vector

        UniValue params;
        if (gArgs.GetArg<bool>("-named", DEFAULT_NAMED))
        {
            params = RPCConvertNamedValues(strMethod, args);
        } else
        {
            params = RPCConvertValues(strMethod, args);
        }

        // Execute and handle connection failures with -rpcwait
        const bool fWait = gArgs.GetArg<bool>("-rpcwait", false);
        do
        {
            try
            {
                const UniValue reply = CallRPC(strMethod, params);

                // Parse reply
                const UniValue &result = find_value(reply, "result");
                const UniValue &error = find_value(reply, "error");

                if (!error.isNull())
                {
                    // Error
                    int code = error["code"].get_int();
                    if (fWait && code == RPC_IN_WARMUP)
                        throw CConnectionFailed("server in warmup");
                    strPrint = "error: " + error.write();
                    nRet = abs(code);
                    if (error.isObject())
                    {
                        UniValue errCode = find_value(error, "code");
                        UniValue errMsg = find_value(error, "message");
                        strPrint = errCode.isNull() ? "" : "error code: " + errCode.getValStr() + "\n";

                        if (errMsg.isStr())
                            strPrint += "error message:\n" + errMsg.get_str();

                        if (errCode.isNum() && errCode.get_int() == RPC_WALLET_NOT_SPECIFIED)
                        {
                            strPrint += "\nTry adding \"-rpcwallet=<filename>\" option to bitcoin-cli command line.";
                        }
                    }
                } else
                {
                    // Result
                    if (result.isNull())
                        strPrint = "";
                    else if (result.isStr())
                        strPrint = result.get_str();
                    else
                        strPrint = result.write(2);
                }
                // Connection succeeded, no need to retry.
                break;
            }
            catch (const CConnectionFailed &)
            {
                if (fWait)
                    MilliSleep(1000);
                else
                    throw;
            }
        } while (fWait);
    }
    catch (const boost::thread_interrupted &)
    {
        throw;
    }
    catch (const std::exception &e)
    {
        strPrint = std::string("error: ") + e.what();
        nRet = EXIT_FAILURE;
    }
    catch (...)
    {
        PrintExceptionContinue(nullptr, "CommandLineRPC()");
        throw;
    }

    if (strPrint != "")
    {
        fprintf((nRet == 0 ? stdout : stderr), "%s\n", strPrint.c_str());
    }
    return nRet;
}

int main(int argc, char *argv[])
{
    SetupEnvironment();
    if (!SetupNetworking())
    {
        fprintf(stderr, "Error: Initializing networking failed\n");
        return EXIT_FAILURE;
    }

    try
    {
        int ret = AppInitRPC(argc, argv);
        if (ret != CONTINUE_EXECUTION)
            return ret;
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "AppInitRPC()");
        return EXIT_FAILURE;
    } catch (...)
    {
        PrintExceptionContinue(nullptr, "AppInitRPC()");
        return EXIT_FAILURE;
    }

    int ret = EXIT_FAILURE;
    try
    {
        ret = CommandLineRPC(argc, argv);
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "CommandLineRPC()");
    } catch (...)
    {
        PrintExceptionContinue(nullptr, "CommandLineRPC()");
    }
    return ret;
}