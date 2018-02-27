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
#include "mempool/txmempool.h"
#include "walletcomponent.h"

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

static bool InitializeLogging()
{
    bool bOk = true;
    try
    {
        log4cpp::PropertyConfigurator::configure("cppconf.ini");
    } catch (log4cpp::ConfigureFailure &f)
    {
        std::cout << f.what() << std::endl;
        std::cout << "using default log conf" << std::endl;
        bOk = false;
    }

    if (!bOk)
    {
        try
        {
            log4cpp::PatternLayout *pLayout1 = new log4cpp::PatternLayout();//创建一个Layout;
            pLayout1->setConversionPattern("%d: %p  %x: %m%n");//指定布局格式;

            log4cpp::PatternLayout *pLayout2 = new log4cpp::PatternLayout();
            pLayout2->setConversionPattern("%d: %p  %x: %m%n");

            log4cpp::RollingFileAppender *rollfileAppender = new log4cpp::RollingFileAppender(
                    "rollfileAppender", "sbtc.log", 100 * 1024, 1);
            rollfileAppender->setLayout(pLayout1);
            log4cpp::Category &root = log4cpp::Category::getRoot().getInstance("RootName");//从系统中得到Category的根;
            root.addAppender(rollfileAppender);
            root.setPriority(log4cpp::Priority::NOTICE);//设置Category的优先级;
            log4cpp::OstreamAppender *osAppender = new log4cpp::OstreamAppender("osAppender", &std::cout);
            osAppender->setLayout(pLayout2);
            root.addAppender(osAppender);
            root.notice("log conf is using defalt !");
        } catch (...)
        {
            return false;
        }
    }

    return true;
}


int main(int argc, char **argv)
{
    InitializeLogging();

    CApp &app = appbase::CApp::Instance();
    app.RegisterComponent(new CChainCommonent);
    app.RegisterComponent(new CTxMemPool);
    app.RegisterComponent(new CHttpRpcComponent);
    app.RegisterComponent(new CNetComponent);
    app.RegisterComponent(new CWalletComponent);
    app.Initialize(argc, argv) && app.Startup() && app.Run();
    app.Shutdown();
    return 0;
}
