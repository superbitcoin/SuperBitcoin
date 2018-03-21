#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>
#include <thread>
#include <vector>
#include <list>
#include "baseimpl.hpp"
#include "utils/util.h"
#include "config/chainparams.h"
#include "sbtccore/clientversion.h"
#include "rpc/client.h"
#include "rpc/protocol.h"
#include "utils/utilstrencodings.h"
#include "utils/net/events.h"

#define  COMMAND_ARG_SEP '`'

static const char DEFAULT_RPCCONNECT[] = "127.0.0.1";
static const bool DEFAULT_NAMED = false;
static const int  DEFAULT_HTTP_CLIENT_TIMEOUT = 900;

class CConnectionFailed : public std::runtime_error
{
public:
    explicit inline CConnectionFailed(const std::string &msg) : std::runtime_error(msg)
    {
    }
};

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

void CApp::InitOptionMap()
{
    const auto defaultChainParams = CreateChainParams(CChainParams::MAIN);
    const auto testnetChainParams = CreateChainParams(CChainParams::TESTNET);

    std::map<string, vector<option_item>> optionMap;

    vector<option_item> item = {
            {"help,h",  "Print this message and exit."},
            {"?",       "Print this message and exit."},
            {"version", "Print version and exit"},
            {"conf",    bpo::value<string>(),
                    strprintf(_("Specify configuration file (default: %s)"), BITCOIN_CONF_FILENAME)},
            {"datadir", bpo::value<string>(), "Specify data directory"}

    };
    optionMap.emplace("configuration options:", item);

    item = {
            {"testnet", bpo::value<string>(), "Use the test chain"},
            {"regtest", bpo::value<string>(), "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
                                                      "This is intended for regression testing tools and app_bpo development."}

    };
    optionMap.emplace("Chain selection options:", item);

    item = {
            {"named",            bpo::value<string>(),
                    strprintf(_("Pass named instead of positional arguments (default: %s)"), DEFAULT_NAMED)},
            {"rpcconnect",       bpo::value<string>(),
                    strprintf(_("Send commands to node running on <ip> (default: %s)"), DEFAULT_RPCCONNECT)},
            {"rpcport",          bpo::value<int>(),
                    strprintf(_("Connect to JSON-RPC on <port> (default: %u or testnet: %u)"),
                              defaultChainParams->RPCPort(),
                              testnetChainParams->RPCPort())},
            {"rpcwait",          bpo::value<string>(), "Wait for RPC server to start"},
            {"rpcuser",          bpo::value<string>(), "Username for JSON-RPC connections"},
            {"rpcpassword",      bpo::value<string>(), "Password for JSON-RPC connections"},
            {"rpcclienttimeout", bpo::value<int>(),
                    strprintf(_("Timeout in seconds during HTTP requests, or 0 for no timeout. (default: %d)"),
                              DEFAULT_HTTP_CLIENT_TIMEOUT)},
            {"stdin",            bpo::value<string>(),
                                                       "Read extra arguments from standard input, one per line until EOF/Ctrl-D (recommended for sensitive information such as passphrases)"},
            {"rpcwallet",        bpo::value<string>(),
                                                       "Send RPC for non-default wallet on RPC server (argument is wallet filename in bitcoind directory, required if bitcoind/-Qt runs with multiple wallets)"}
    };
    optionMap.emplace("rpc options:", item);

    item = {
            {"commandname",      bpo::value<string>(), "Command Name, internal option, INVALID FOR COMMANDLINE!"},
            {"commandargs",      bpo::value<string>(), "Command arguments, internal option, INVALID FOR COMMANDLINE!"}
    };
    optionMap.emplace("command options:", item);

    std::string strHead =
            strprintf(_("%s RPC client version"), _(PACKAGE_NAME)) + " " + FormatFullVersion() + "\n" + "\n" +
            _("Usage:") + "\n" +
            "  bitcoin-cli [options] <command> [params]  " + strprintf(_("Send command to %s"), _(PACKAGE_NAME)) +
            "\n" +
            "  bitcoin-cli [options] -named <command> [name=value] ... " +
            strprintf(_("Send command to %s (with named arguments)"), _(PACKAGE_NAME)) + "\n";
    pArgs->SetOptionName(strHead);
    pArgs->SetOptionTable(optionMap);
}

void CApp::RelayoutArgs(int& argc, char**& argv)
{
    static std::list<std::string> _argx;
    static std::vector<const char*> _argv;

    _argv.emplace_back(argv[0]); // assert(argc >= 1)

    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++)
    {
        if (strlen(argv[i]) > 2 && argv[i][1] != '-')
        {
            _argx.emplace_back('-' + std::string(argv[i]));
            _argv.emplace_back(_argx.back().c_str());
        }
        else
        {
            _argv.emplace_back(argv[i]);
        }
    }

    if (i < argc)
    {
        _argx.emplace_back(std::string("--commandname=") + argv[i++]);
        _argv.emplace_back(_argx.back().c_str());
    }

    if (i < argc)
    {
        std::string commandargs(argv[i++]);
        for (; i < argc; i++)
        {
            commandargs += COMMAND_ARG_SEP;
            commandargs += argv[i];
        }
        _argx.emplace_back(std::string("--commandargs=") + commandargs);
        _argv.emplace_back(_argx.back().c_str());
    }

    argc = (int)_argv.size();
    argv = (char**)&_argv[0];
}

bool CApp::Run()
{
    if (pArgs->GetArg<bool>("-rpcssl", false))
    {
        fprintf(stderr, "Error: SSL mode for RPC (-rpcssl) is no longer supported.\n");
        return false;
    }

    if (!pArgs->IsArgSet("-commandname"))
    {
        fprintf(stderr, "too few parameters (need at least command).\n");
        return false;
    }

    bool nRet = 0;
    std::string strPrint;
    std::string strMethod = pArgs->GetArg<std::string>("-commandname", "");
    std::string strArgs = pArgs->GetArg<std::string>("-commandargs", "");
    std::vector<std::string> args = SplitString(strArgs, COMMAND_ARG_SEP);

    try
    {
        if (pArgs->GetArg<bool>("-stdin", false))
        {
            // Read one arg per line from stdin and append
            std::string line;
            while (std::getline(std::cin, line))
                args.push_back(line);
        }

        UniValue params;
        if (pArgs->GetArg<bool>("-named", DEFAULT_NAMED))
        {
            params = RPCConvertNamedValues(strMethod, args);
        } else
        {
            params = RPCConvertValues(strMethod, args);
        }

        // Execute and handle connection failures with -rpcwait
        const bool fWait = pArgs->GetArg<bool>("-rpcwait", false);
        do
        {
            try
            {
                const UniValue reply = CallRPC(strMethod, params);
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
        strPrint = "thread interrupted!";
        nRet = EXIT_FAILURE;
    }
    catch (const std::exception &e)
    {
        strPrint = std::string("error: ") + e.what();
        nRet = EXIT_FAILURE;
    }
    catch (...)
    {
        PrintExceptionContinue(nullptr, "CommandLineRPC()");
        nRet = EXIT_FAILURE;
    }

    if (!strPrint.empty())
    {
        fprintf((nRet == EXIT_SUCCESS ? stdout : stderr), "%s\n", strPrint.c_str());
    }
    return nRet == EXIT_SUCCESS;
}

UniValue CApp::CallRPC(const std::string &strMethod, const UniValue &params)
{
    std::string host;
    // In preference order, we choose the following for the port:
    //     1. -rpcport
    //     2. port in -rpcconnect (ie following : in ipv4 or ]: in ipv6)
    //     3. default port for chain
    int port = pChainParams->RPCPort();
    SplitHostPort(pArgs->GetArg<std::string>("-rpcconnect", DEFAULT_RPCCONNECT), port, host);
    port = pArgs->GetArg<int>("-rpcport", port);

    // Obtain event base
    raii_event_base base = obtain_event_base();

    // Synchronously look up hostname
    raii_evhttp_connection evcon = obtain_evhttp_connection_base(base.get(), host, port);
    evhttp_connection_set_timeout(evcon.get(), pArgs->GetArg<int>("-rpcclienttimeout", DEFAULT_HTTP_CLIENT_TIMEOUT));

    HTTPReply response;
    raii_evhttp_request req = obtain_evhttp_request(http_request_done, (void *)&response);
    if (req == nullptr)
        throw std::runtime_error("create http request failed");
#if LIBEVENT_VERSION_NUMBER >= 0x02010300
    evhttp_request_set_error_cb(req.get(), http_error_cb);
#endif

    // Get credentials
    std::string strRPCUserColonPass;
    if (pArgs->GetArg<std::string>("-rpcpassword", "") == "")
    {
        // Try fall back to cookie-based authentication if no password is provided
        if (!GetAuthCookie(&strRPCUserColonPass))
        {
            throw std::runtime_error(strprintf(
                    _("Could not locate RPC credentials. No authentication cookie could be found, and no rpcpassword is set in the configuration file (%s)"),
                    GetConfigFile(
                            pArgs->GetArg<std::string>("-conf", std::string(BITCOIN_CONF_FILENAME))).string().c_str()));

        }
    } else
    {
        strRPCUserColonPass =
                pArgs->GetArg<std::string>("-rpcuser", "") + ":" + pArgs->GetArg<std::string>("-rpcpassword", "");
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
    std::string walletName = pArgs->GetArg<std::string>("-rpcwallet", "");
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

