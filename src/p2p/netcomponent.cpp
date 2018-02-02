#include <iostream>
#include <vector>
#include <string>
#include "netcomponent.h"
#include "base.hpp"
#include "addrman.h"
#include "netbase.h"
#include "net.h"
#include "net_processing.h"
#include "torcontrol.h"
#include "config/argmanager.h"

CNetComponent::CNetComponent()
{
}

CNetComponent::~CNetComponent()
{
}

bool CNetComponent::ComponentInitialize()
{
    std::cout << "initialize net component \n";

    netConnMgr.reset(new CConnman(GetRand(std::numeric_limits<uint64_t>::max()),
                                  GetRand(std::numeric_limits<uint64_t>::max())));

    CScheduler scheduler;
    peerLogic.reset(new PeerLogicValidation(netConnMgr.get(), scheduler));
    /// RegisterValidationInterface(peerLogic.get());

    CArgsManager& cmdlineArgs = *appbase::CBase::Instance().GetArgsManager();

    // sanitize comments per BIP-0014, format user agent and check total size
    std::vector<std::string> uacomments;
    for (const std::string &cmt : cmdlineArgs.GetArgs("-uacomment"))
    {
        if (cmt != SanitizeString(cmt, SAFE_CHARS_UA_COMMENT))
            return InitError(strprintf(_("User Agent comment (%s) contains unsafe characters."), cmt));
        uacomments.push_back(cmt);
    }
    strSubVersion = FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, uacomments);
    if (strSubVersion.size() > MAX_SUBVERSION_LENGTH)
    {
        return InitError(strprintf(
                _("Total length of network version string (%i) exceeds maximum length (%i). Reduce the number or size of uacomments."),
                strSubVersion.size(), MAX_SUBVERSION_LENGTH));
    }

    if (cmdlineArgs.IsArgSet("-onlynet"))
    {
        std::set<enum Network> nets;
        for (const std::string &snet : cmdlineArgs.GetArgs("-onlynet"))
        {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
                return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++)
        {
            enum Network net = (enum Network)n;
            if (!nets.count(net))
                SetLimited(net);
        }
    }

    // Check for host lookup allowed before parsing any network related parameters
    fNameLookup = cmdlineArgs.GetArg("-dns", DEFAULT_NAME_LOOKUP);

    bool proxyRandomize = cmdlineArgs.GetArg<int>("-proxyrandomize", DEFAULT_PROXYRANDOMIZE);
    // -proxy sets a proxy for all outgoing network traffic
    // -noproxy (or -proxy=0) as well as the empty string can be used to not set a proxy, this is the default
    std::string proxyArg = cmdlineArgs.GetArg<std::string>("-proxy", "");
    SetLimited(NET_TOR);
    if (proxyArg != "" && proxyArg != "0")
    {
        CService proxyAddr;
        if (!Lookup(proxyArg.c_str(), proxyAddr, 9050, fNameLookup))
        {
            return InitError(strprintf(_("Invalid -proxy address or hostname: '%s'"), proxyArg));
        }

        proxyType addrProxy = proxyType(proxyAddr, proxyRandomize);
        if (!addrProxy.IsValid())
            return InitError(strprintf(_("Invalid -proxy address or hostname: '%s'"), proxyArg));

        SetProxy(NET_IPV4, addrProxy);
        SetProxy(NET_IPV6, addrProxy);
        SetProxy(NET_TOR, addrProxy);
        SetNameProxy(addrProxy);
        SetLimited(NET_TOR, false); // by default, -proxy sets onion as reachable, unless -noonion later
    }

    // -onion can be used to set only a proxy for .onion, or override normal proxy for .onion addresses
    // -noonion (or -onion=0) disables connecting to .onion entirely
    // An empty string is used to not override the onion proxy (in which case it defaults to -proxy set above, or none)
    std::string onionArg = cmdlineArgs.GetArg<std::string>("-onion", "");
    if (onionArg != "")
    {
        if (onionArg == "0")
        { // Handle -noonion/-onion=0
            SetLimited(NET_TOR); // set onions as unreachable
        } else
        {
            CService onionProxy;
            if (!Lookup(onionArg.c_str(), onionProxy, 9050, fNameLookup))
            {
                return InitError(strprintf(_("Invalid -onion address or hostname: '%s'"), onionArg));
            }
            proxyType addrOnion = proxyType(onionProxy, proxyRandomize);
            if (!addrOnion.IsValid())
                return InitError(strprintf(_("Invalid -onion address or hostname: '%s'"), onionArg));
            SetProxy(NET_TOR, addrOnion);
            SetLimited(NET_TOR, false);
        }
    }

    // see Step 2: parameter interactions for more information about these
    fListen = cmdlineArgs.GetArg<bool>("-listen", DEFAULT_LISTEN);
    fDiscover = cmdlineArgs.GetArg<bool>("-discover", true);
    fRelayTxes = !cmdlineArgs.GetArg<bool>("-blocksonly", DEFAULT_BLOCKSONLY);

    for (const std::string &strAddr : cmdlineArgs.GetArgs("-externalip"))
    {
        CService addrLocal;
        if (Lookup(strAddr.c_str(), addrLocal, GetListenPort(), fNameLookup) && addrLocal.IsValid())
            AddLocal(addrLocal, LOCAL_MANUAL);
        else
            return InitError(strprintf(_("Cannot resolve externalip address: '%s'"), strAddr));
    }

#if ENABLE_ZMQ
    pzmqNotificationInterface = CZMQNotificationInterface::Create();

    if (pzmqNotificationInterface) {
        RegisterValidationInterface(pzmqNotificationInterface);
    }
#endif

    uint64_t nMaxOutboundLimit = 0; //unlimited unless -maxuploadtarget is set
    uint64_t nMaxOutboundTimeframe = MAX_UPLOAD_TIMEFRAME;

    if (cmdlineArgs.IsArgSet("-maxuploadtarget"))
    {
        nMaxOutboundLimit = cmdlineArgs.GetArg("maxuploadtarget", DEFAULT_MAX_UPLOAD_TARGET) * 1024 * 1024;
    }

    ///netConnOptions.nLocalServices = nLocalServices;
    ///netConnOptions.nRelevantServices = nRelevantServices;
    ///netConnOptions.nMaxConnections = nMaxConnections;
    ///netConnOptions.nMaxOutbound = std::min(MAX_OUTBOUND_CONNECTIONS, netConnOptions.nMaxConnections);
    netConnOptions.nMaxAddnode = MAX_ADDNODE_CONNECTIONS;
    netConnOptions.nMaxOutboundTimeframe = nMaxOutboundTimeframe;
    netConnOptions.nMaxOutboundLimit = nMaxOutboundLimit;
    netConnOptions.nMaxFeeler = 1;
    /// netConnOptions.nBestHeight = chainActive.Height();
    /// netConnOptions.uiInterface = &uiInterface;
    netConnOptions.m_msgproc = peerLogic.get();
    netConnOptions.nSendBufferMaxSize = 1000 * cmdlineArgs.GetArg("-maxsendbuffer", DEFAULT_MAXSENDBUFFER);
    netConnOptions.nReceiveFloodSize = 1000 * cmdlineArgs.GetArg("-maxreceivebuffer", DEFAULT_MAXRECEIVEBUFFER);

    for (const std::string &strBind : cmdlineArgs.GetArgs("-bind"))
    {
        CService addrBind;
        if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
        {
            return InitError(strprintf(_("Cannot resolve bind address: '%s'"), strBind));
        }
        netConnOptions.vBinds.push_back(addrBind);
    }
    for (const std::string &strBind : cmdlineArgs.GetArgs("-whitebind"))
    {
        CService addrBind;
        if (!Lookup(strBind.c_str(), addrBind, 0, false))
        {
            return InitError(strprintf(_("Cannot resolve whitebind address: '%s'"), strBind));
        }
        if (addrBind.GetPort() == 0)
        {
            return InitError(strprintf(_("Need to specify a port with -whitebind: '%s'"), strBind));
        }
        netConnOptions.vWhiteBinds.push_back(addrBind);
    }

    for (const auto &net : cmdlineArgs.GetArgs("-whitelist"))
    {
        CSubNet subnet;
        LookupSubNet(net.c_str(), subnet);
        if (!subnet.IsValid())
            return InitError(strprintf(_("Invalid netmask specified in -whitelist: '%s'"), net));
        netConnOptions.vWhitelistedRange.push_back(subnet);
    }

    if (cmdlineArgs.IsArgSet("-seednode"))
    {
        netConnOptions.vSeedNodes = cmdlineArgs.GetArgs("-seednode");
    }

    return true;
}

bool CNetComponent::ComponentStartup()
{
    std::cout << "startup net component \n";

    CArgsManager& cmdlineArgs = *appbase::CBase::Instance().GetArgsManager();

    if (cmdlineArgs.GetArg<bool>("listenonion", DEFAULT_LISTEN_ONION))
        ///StartTorControl(threadGroup, scheduler);

    ///Discover(threadGroup);

    // Map ports with UPnP
    MapPort(cmdlineArgs.GetArg<bool>("upnp", DEFAULT_UPNP));

//    if (!netConnMgr->Start(scheduler, netConnOptions))
//    {
//        return false;
//    }

    return true;
}

bool CNetComponent::ComponentShutdown()
{
    std::cout << "shutdown net component \n";

    InterruptTorControl();

    if (netConnMgr)
    {
        netConnMgr->Interrupt();
    }

    if (peerLogic)
    {
        UnregisterValidationInterface(peerLogic.get());
    }

    if (netConnMgr)
    {
        netConnMgr->Stop();
    }

    peerLogic.reset();
    netConnMgr.reset();

    StopTorControl();

    return true;
}
