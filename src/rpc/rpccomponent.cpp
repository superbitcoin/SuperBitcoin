#include <iostream>
#include "rpccomponent.h"
#include "rpc/server.h"
#include "rpc/register.h"
#include "utils/util.h"
#include "utils/net/httpserver.h"
#include "utils/net/httprpc.h"
#include "sbtcd/baseimpl.hpp"
#include "config/argmanager.h"

SET_CPP_SCOPED_LOG_CATEGORY(CID_HTTP_RPC);

CHttpRpcComponent::CHttpRpcComponent()
{

}

CHttpRpcComponent::~CHttpRpcComponent()
{

}

bool CHttpRpcComponent::ComponentInitialize()
{
    ILogFormat("initialize http rpc component.");

    RegisterAllCoreRPCCommands(tableRPC);

    return true;
}

bool CHttpRpcComponent::ComponentStartup()
{
    ILogFormat("startup http rpc component.");

    if (!Args().GetArg<bool>("-server", false))
        return true;

    //RPCServer::OnStarted(&OnRPCStarted);
    //RPCServer::OnStopped(&OnRPCStopped);
    //RPCServer::OnPreCommand(&OnRPCPreCommand);

    if (!InitHTTPServer())
    {
        return rLogError("Unable to init HTTP server. See debug log for details.");
    }

    if (!StartRPC())
    {
        return rLogError("Unable to start RPC server. See debug log for details.");
    }

    if (!StartHTTPRPC())
    {
        return rLogError("Unable to start HTTP RPC server. See debug log for details.");
    }

    if (Args().GetArg<bool>("-rest", false) && !StartREST())
    {
        return rLogError("Unable to start REST server. See debug log for details.");
    }

    if (!StartHTTPServer())
    {
        return rLogError("Unable to start HTTP server. See debug log for details.");
    }

    SetRPCWarmupFinished();

    return true;
}

bool CHttpRpcComponent::ComponentShutdown()
{
    ILogFormat("shutdown http rpc component.");

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

const char *CHttpRpcComponent::whoru() const
{
    return "I am CHttpRpcComponent\n";
}

