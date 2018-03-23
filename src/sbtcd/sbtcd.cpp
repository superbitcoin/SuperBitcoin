#include "baseimpl.hpp"
#include "p2p/netcomponent.h"
#include "rpc/rpccomponent.h"
#include "chaincontrol/chaincomponent.h"
#include "mempool/mempoolcomponent.h"
#include "wallet/walletcomponent.h"
#include "contract-api/contractcomponent.h"

CApp gApp;

appbase::IBaseApp *GetApp()
{
    return &gApp;
}

int main(int argc, char **argv)
{
    gApp.RelayoutArgs(argc, argv);
    gApp.RegisterComponent(new CChainComponent);
    gApp.RegisterComponent(new CContractComponent);
    gApp.RegisterComponent(new CMempoolComponent);
    gApp.RegisterComponent(new CHttpRpcComponent);
    gApp.RegisterComponent(new CNetComponent);
    gApp.RegisterComponent(new CWalletComponent);
    gApp.Initialize(argc, argv) && gApp.Startup() && gApp.Run();
    gApp.Shutdown();

    return 0;
}
