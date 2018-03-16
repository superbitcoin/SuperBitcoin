#include <iostream>
#include "rpccomponent.h"
#include "rpc/server.h"
#include "rpc/register.h"
#include "utils/util.h"
#include "utils/net/httpserver.h"
#include "utils/net/httprpc.h"
#include "sbtcd/baseimpl.hpp"
#include "config/argmanager.h"


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

    if (!Args().GetArg<bool>("-server", false))
        return true;

    //    RPCServer::OnStarted(&OnRPCStarted);
    //    RPCServer::OnStopped(&OnRPCStopped);
    //    RPCServer::OnPreCommand(&OnRPCPreCommand);

    if (!InitHTTPServer())
    {
        mlog_error("Unable to init HTTP server. See debug log for details.");
        return false;
    }

    if (!StartRPC())
    {
        mlog_error("Unable to start RPC server. See debug log for details.");
        return false;
    }

    if (!StartHTTPRPC())
    {
        mlog_error("Unable to start HTTP RPC server. See debug log for details.");
        return false;
    }

    if (Args().GetArg<bool>("-rest", false) && !StartREST())
    {
        mlog_error("Unable to start REST server. See debug log for details.");
        return false;
    }

    if (!StartHTTPServer())
    {
        mlog_error("Unable to start HTTP server. See debug log for details.");
        return false;
    }

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

const char *CHttpRpcComponent::whoru() const
{
    return "I am CHttpRpcComponent\n";
}

