/*************************************************
 * File name:		// sbtcd.cpp
 * Author:
 * Date: 		    //  2018.01.26
 * Description:		// Superbitcoin program framework master logic

 * Others:		    //
 * History:		    // 2018.01.26

 * 1. Date:
 * Author:
 * Modification:
*************************************************/
#include "base.hpp"
#include "p2p/netcomponent.h"
#include "rpc/rpccomponent.h"
#include "chaincontrol/chaincomponent.h"
#include "mempool/mempoolcomponent.h"
#include "wallet/walletcomponent.h"

/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of the reference client for an experimental new digital currency called Bitcoin (https://www.bitcoin.org/),
 * which enables instant payments to anyone, anywhere in the world. Bitcoin uses peer-to-peer technology to operate
 * with no central authority: managing transactions and issuing money are carried out collectively by the network.
 *
 * The software is a community-driven open source project, released under the MIT license.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or <code>Files</code> at the top of the page to start navigating the code.
 */

#include "log4cpp/Category.hh"
#include "log4cpp/PropertyConfigurator.hh"
#include "log4cpp/PatternLayout.hh"
#include "log4cpp/RollingFileAppender.hh"
#include "log4cpp/OstreamAppender.hh"


int main(int argc, char **argv)
{
    CApp &app = appbase::CApp::Instance();
    app.RegisterComponent(new CChainComponent);
    app.RegisterComponent(new CMempoolComponent);
    app.RegisterComponent(new CHttpRpcComponent);
    app.RegisterComponent(new CNetComponent);
    app.RegisterComponent(new CWalletComponent);
    app.Initialize(argc, argv) && app.Startup() && app.Run();
    app.Shutdown();

    return 0;
}
