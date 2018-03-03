#include <iostream>
#include "rpccomponent.h"
#include "rpc/server.h"
#include "rpc/register.h"
#include "utils/util.h"
#include "utils/net/httpserver.h"
#include "utils/net/httprpc.h"
#include "framework/base.hpp"
#include "config/argmanager.h"

log4cpp::Category &CHttpRpcComponent::mlog = log4cpp::Category::getInstance(EMTOSTR(CID_HTTP_RPC));

CHttpRpcComponent::CHttpRpcComponent()
{

}

CHttpRpcComponent::~CHttpRpcComponent()
{

}

bool CHttpRpcComponent::ComponentInitialize()
{
    mlog_notice("initialize http rpc component.");

    RegisterAllCoreRPCCommands(tableRPC);

    return true;
}

bool CHttpRpcComponent::ComponentStartup()
{
    mlog_notice("startup http rpc component.");

    const CArgsManager& appArgs = app().GetArgsManager();

    if (!appArgs.GetArg<bool>("-server", false))
        return true;

//    RPCServer::OnStarted(&OnRPCStarted);
//    RPCServer::OnStopped(&OnRPCStopped);
//    RPCServer::OnPreCommand(&OnRPCPreCommand);

    if (!InitHTTPServer())
        return error("Unable to init HTTP server. See debug log for details.");

    if (!StartRPC())
        return error("Unable to start RPC server. See debug log for details.");

    if (!StartHTTPRPC())
        return error("Unable to start HTTP RPC server. See debug log for details.");

    if (appArgs.GetArg<bool>("-rest", false) && !StartREST())
        return error("Unable to start REST server. See debug log for details.");

    if (!StartHTTPServer())
        return error("Unable to start HTTP server. See debug log for details.");

    SetRPCWarmupFinished();

    return true;
}

bool CHttpRpcComponent::ComponentShutdown()
{
    mlog_notice("shutdown http rpc component.");

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

