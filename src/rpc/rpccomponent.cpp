#include <iostream>
#include "rpccomponent.h"
#include "rpc/server.h"
#include "rpc/register.h"
#include "utils/net/httpserver.h"
#include "utils/net/httprpc.h"
#include "framework/base.hpp"
#include "config/argmanager.h"

CHttpRpcComponent::CHttpRpcComponent()
{

}

CHttpRpcComponent::~CHttpRpcComponent()
{

}

bool CHttpRpcComponent::ComponentInitialize()
{
    std::cout << "initialize http rpc component \n";

    RegisterAllCoreRPCCommands(tableRPC);

    return true;
}

bool CHttpRpcComponent::ComponentStartup()
{
    std::cout << "startup http rpc component \n";

    CArgsManager& appArgs = *appbase::app().GetArgsManager();

    if (!appArgs.GetArg<bool>("-server", false))
        return true;

//    RPCServer::OnStarted(&OnRPCStarted);
//    RPCServer::OnStopped(&OnRPCStopped);
//    RPCServer::OnPreCommand(&OnRPCPreCommand);

    if (!InitHTTPServer())
        return InitError(_("Unable to init HTTP server. See debug log for details."));

    if (!StartRPC())
        return InitError(_("Unable to start RPC server. See debug log for details."));

    if (!StartHTTPRPC())
        return InitError(_("Unable to start HTTP RPC server. See debug log for details."));

    if (appArgs.GetArg<bool>("-rest", DEFAULT_REST_ENABLE) && !StartREST())
        return InitError(_("Unable to start REST server. See debug log for details."));

    if (!StartHTTPServer())
        return InitError(_("Unable to start HTTP server. See debug log for details."));

    SetRPCWarmupFinished();

    return true;
}

bool CHttpRpcComponent::ComponentShutdown()
{
    std::cout << "shutdown http rpc component \n";

    InterruptHTTPServer();
    InterruptHTTPRPC();
    InterruptRPC();
    InterruptREST();

    StopHTTPRPC();
    StopREST();
    StopRPC();
    StopHTTPServer();

    return true;
}

const char* CHttpRpcComponent::whoru() const
{
    return "I am CHttpRpcComponent\n";
}

