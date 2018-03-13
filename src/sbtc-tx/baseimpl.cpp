#include <thread>
#include <chrono>
#include <functional>
#include <signal.h>
#include <boost/interprocess/sync/file_lock.hpp>
#include <log4cpp/PropertyConfigurator.hh>
#include <log4cpp/PatternLayout.hh>
#include <log4cpp/RollingFileAppender.hh>
#include <log4cpp/OstreamAppender.hh>
#include "baseimpl.hpp"
#include "noui.h"
#include "config/chainparamsbase.h"
#include "config/chainparams.h"
#include "compat/sanity.h"
#include "config/sbtc-config.h"
#include "sbtccore/clientversion.h"
#include "utils/util.h"
#include "utils/utilstrencodings.h"
#include "rpc/server.h"
#include "p2p/net.h"
#include "p2p/netbase.h"
#include "utils/arith_uint256.h"
#include "utils/utilmoneystr.h"
#include "utils/net/torcontrol.h"
#include "wallet/key.h"
#include "wallet/feerate.h"
#include "sbtccore/block/validation.h"
#include "sbtccore/transaction/policy.h"
#include "config/consensus.h"
#include "framework/validationinterface.h"

CApp::CApp()
{
    nVersion = 1;
    bShutdown = false;
}

bool CApp::AppInitialize()
{

    return true;
}

void CApp::InitOptionMap()
{

}

