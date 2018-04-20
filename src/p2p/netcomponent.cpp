#include <vector>
#include <string>
#include "netcomponent.h"
#include "addrman.h"
#include "netbase.h"
#include "net.h"
#include "net_processing.h"
#include "torcontrol.h"
#include "config/argmanager.h"
#include "utils/util.h"
#include "utils/utilstrencodings.h"
#include "sbtccore/clientversion.h"
#include "interface/ichaincomponent.h"

SET_CPP_SCOPED_LOG_CATEGORY(CID_P2P_NET);

std::unique_ptr<CConnman, NoopDeleter<CConnman>> g_connman;

CNetComponent::CNetComponent()
{
}

CNetComponent::~CNetComponent()
{
}

bool CNetComponent::ComponentInitialize()
{
    ILogFormat("initialize p2p net component.");

    if (!SetupNetworking())
    {
        return rLogError("Initializing networking failed");
    }

    GET_CHAIN_INTERFACE(ifChainObj);

    netConnMgr.reset(new CConnman(GetRand(std::numeric_limits<uint64_t>::max()),
                                  GetRand(std::numeric_limits<uint64_t>::max())));

    peerLogic.reset(new PeerLogicValidation(netConnMgr.get(), GetApp()->GetScheduler()));
    RegisterValidationInterface(peerLogic.get());

    g_connman.reset(netConnMgr.get());

    // sanitize comments per BIP-0014, format user agent and check total size
    std::vector<std::string> uacomments;
    for (const std::string &cmt : Args().GetArgs("-uacomment"))
    {
        if (cmt != SanitizeString(cmt, SAFE_CHARS_UA_COMMENT))
        {
            return rLogError("User Agent comment (%s) contains unsafe characters.", cmt);
        }
        uacomments.push_back(cmt);
    }
    strSubVersion = FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, uacomments);
    if (strSubVersion.size() > MAX_SUBVERSION_LENGTH)
    {
        return rLogError(
                "Total length of network version string (%i) exceeds maximum length (%i). Reduce the number or size of uacomments.",
                strSubVersion.size(), MAX_SUBVERSION_LENGTH);
    }

    if (Args().IsArgSet("-onlynet"))
    {
        std::set<enum Network> nets;
        for (const std::string &snet : Args().GetArgs("-onlynet"))
        {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
            {
                return rLogError("Unknown network specified in -onlynet: '%s'", snet);
            }

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
    fNameLookup = Args().GetArg("-dns", DEFAULT_NAME_LOOKUP);

    bool proxyRandomize = Args().GetArg<bool>("-proxyrandomize", true);
    // -proxy sets a proxy for all outgoing network traffic
    // -noproxy (or -proxy=0) as well as the empty string can be used to not set a proxy, this is the default
    std::string proxyArg = Args().GetArg<std::string>("-proxy", "");
    SetLimited(NET_TOR);
    if (proxyArg != "" && proxyArg != "0")
    {
        CService proxyAddr;
        if (!Lookup(proxyArg.c_str(), proxyAddr, 9050, fNameLookup))
        {
            return rLogError("Invalid -proxy address or hostname: '%s'", proxyArg);
        }

        proxyType addrProxy = proxyType(proxyAddr, proxyRandomize);
        if (!addrProxy.IsValid())
        {
            return rLogError("Invalid -proxy address or hostname: '%s'", proxyArg);
        }

        SetProxy(NET_IPV4, addrProxy);
        SetProxy(NET_IPV6, addrProxy);
        SetProxy(NET_TOR, addrProxy);
        SetNameProxy(addrProxy);
        SetLimited(NET_TOR, false); // by default, -proxy sets onion as reachable, unless -noonion later
    }

    // -onion can be used to set only a proxy for .onion, or override normal proxy for .onion addresses
    // -noonion (or -onion=0) disables connecting to .onion entirely
    // An empty string is used to not override the onion proxy (in which case it defaults to -proxy set above, or none)
    std::string onionArg = Args().GetArg<std::string>("-onion", "");
    if (onionArg != "")
    {
        if (onionArg == "0")
        {
            // Handle -noonion/-onion=0
            SetLimited(NET_TOR); // set onions as unreachable
        } else
        {
            CService onionProxy;
            if (!Lookup(onionArg.c_str(), onionProxy, 9050, fNameLookup))
            {
                return rLogError("Invalid -onion address or hostname: '%s'", onionArg);
            }

            proxyType addrOnion = proxyType(onionProxy, proxyRandomize);
            if (!addrOnion.IsValid())
            {
                return rLogError("Invalid -onion address or hostname: '%s'", onionArg);
            }

            SetProxy(NET_TOR, addrOnion);
            SetLimited(NET_TOR, false);
        }
    }

    // see Step 2: parameter interactions for more information about these
    fListen = Args().GetArg<bool>("-listen", DEFAULT_LISTEN);
    fDiscover = Args().GetArg<bool>("-discover", true);
    fRelayTxes = !Args().GetArg<bool>("-blocksonly", DEFAULT_BLOCKSONLY);

    for (const std::string &strAddr : Args().GetArgs("-externalip"))
    {
        CService addrLocal;
        if (Lookup(strAddr.c_str(), addrLocal, GetListenPort(), fNameLookup) && addrLocal.IsValid())
            AddLocal(addrLocal, LOCAL_MANUAL);
        else
        {
            return rLogError("Cannot resolve externalip address: '%s'", strAddr);
        }
    }

#if ENABLE_ZMQ
    pzmqNotificationInterface = CZMQNotificationInterface::Create();

    if (pzmqNotificationInterface) {
        RegisterValidationInterface(pzmqNotificationInterface);
    }
#endif
    return true;
}

bool CNetComponent::ComponentStartup()
{
    ILogFormat("startup p2p net component.");

    if (!netConnMgr || !peerLogic)
    {
        return false;
    }

    if (Args().GetArg<bool>("listenonion", DEFAULT_LISTEN_ONION))
    {
        StartTorControl();
    }

    Discover();

    // Map ports with UPnP
    MapPort(Args().GetArg<bool>("upnp", DEFAULT_UPNP));



    GET_CHAIN_INTERFACE(ifChainObj);

    uint64_t nMaxOutboundLimit = 0; //unlimited unless -maxuploadtarget is set
    uint64_t nMaxOutboundTimeframe = MAX_UPLOAD_TIMEFRAME;

    if (Args().IsArgSet("-maxuploadtarget"))
    {
        nMaxOutboundLimit = Args().GetArg("maxuploadtarget", DEFAULT_MAX_UPLOAD_TARGET) * 1024 * 1024;
    }

    // -bind and -whitebind can't be set when not listening
    size_t nUserBind = Args().GetArgs("-bind").size() + Args().GetArgs("-whitebind").size();
    if (nUserBind != 0 && !Args().GetArg<bool>("-listen", DEFAULT_LISTEN))
    {
        return rLogError("Cannot set -bind or -whitebind together with -listen=0");
    }

    // Make sure enough file descriptors are available
    int nBind = std::max(nUserBind, size_t(1));
    int nUserMaxConnections = Args().GetArg("-maxconnections", DEFAULT_MAX_PEER_CONNECTIONS);
    int nMaxConnections = std::max(nUserMaxConnections, 0);

#ifdef WIN32
    // Win32 LevelDB doesn't use filedescriptors, and the ones used for
    // accessing block files don't count towards the fd_set size limit
    // anyway.
# define MIN_CORE_FILEDESCRIPTORS 0
#else
# define MIN_CORE_FILEDESCRIPTORS 150
#endif

    // Trim requested connection counts, to fit into system limitations
    nMaxConnections = std::max(
            std::min(nMaxConnections, (int)(FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS - MAX_ADDNODE_CONNECTIONS)),
            0);
    int nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS + MAX_ADDNODE_CONNECTIONS);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
    {
        return rLogError("Not enough file descriptors available.");
    }

    nMaxConnections = std::min(nFD - MIN_CORE_FILEDESCRIPTORS - MAX_ADDNODE_CONNECTIONS, nMaxConnections);

    if (nMaxConnections < nUserMaxConnections)
    {
        WLogFormat("Reducing -maxconnections from %d to %d, because of system limitations.", nUserMaxConnections,
                   nMaxConnections);
    }

    ILogFormat("Using at most %i automatic connections (%i file descriptors available)", nMaxConnections, nFD);

    netConnOptions.nLocalServices = nLocalServices;
    netConnOptions.nRelevantServices = nRelevantServices;
    netConnOptions.nMaxConnections = nMaxConnections;
    netConnOptions.nMaxOutbound = std::min(MAX_OUTBOUND_CONNECTIONS, nMaxConnections);
    netConnOptions.nMaxAddnode = MAX_ADDNODE_CONNECTIONS;
    netConnOptions.nMaxOutboundTimeframe = nMaxOutboundTimeframe;
    netConnOptions.nMaxOutboundLimit = nMaxOutboundLimit;
    netConnOptions.nMaxFeeler = 1;
    netConnOptions.nBestHeight = ifChainObj->GetActiveChainHeight();
    netConnOptions.uiInterface = &GetApp()->GetUIInterface();
    netConnOptions.m_msgproc = peerLogic.get();
    netConnOptions.nSendBufferMaxSize = 1000 * Args().GetArg("-maxsendbuffer", DEFAULT_MAXSENDBUFFER);
    netConnOptions.nReceiveFloodSize = 1000 * Args().GetArg("-maxreceivebuffer", DEFAULT_MAXRECEIVEBUFFER);

    for (const std::string &strBind : Args().GetArgs("-bind"))
    {
        CService addrBind;
        if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
        {
            return rLogError("Cannot resolve bind address: '%s'", strBind);
        }
        netConnOptions.vBinds.push_back(addrBind);
    }

    for (const std::string &strBind : Args().GetArgs("-whitebind"))
    {
        CService addrBind;
        if (!Lookup(strBind.c_str(), addrBind, 0, false))
        {
            return rLogError("Cannot resolve whitebind address: '%s'", strBind);
        }
        if (addrBind.GetPort() == 0)
        {
            return rLogError("Need to specify a port with -whitebind: '%s'", strBind);
        }
        netConnOptions.vWhiteBinds.push_back(addrBind);
    }

    for (const auto &net : Args().GetArgs("-whitelist"))
    {
        CSubNet subnet;
        LookupSubNet(net.c_str(), subnet);
        if (!subnet.IsValid())
        {
            return rLogError("Invalid netmask specified in -whitelist: '%s'", net);
        }

        netConnOptions.vWhitelistedRange.push_back(subnet);
    }

    if (Args().IsArgSet("-seednode"))
    {
        netConnOptions.vSeedNodes = Args().GetArgs("-seednode");
    }

    return netConnMgr->Start(GetApp()->GetScheduler(), netConnOptions);
}

bool CNetComponent::ComponentShutdown()
{
    ILogFormat("shutdown p2p net component.");

    InterruptTorControl();

    if (netConnMgr)
    {
        netConnMgr->Interrupt();
    }

    MapPort(false);

    if (peerLogic)
    {
        UnregisterValidationInterface(peerLogic.get());
    }

    if (netConnMgr)
    {
        netConnMgr->Stop();
    }

    g_connman.reset();
    peerLogic.reset();
    netConnMgr.reset();

    StopTorControl();

    return true;
}



bool CNetComponent::SendNetMessage(int64_t nodeID, const std::string &command, const std::vector<unsigned char> &data)
{
    if (netConnMgr)
    {
        if (nodeID == -1) // means any node, broadcast.
        {
            netConnMgr->ForEachNode([&](CNode *pnode)
                                    { netConnMgr->PushMessage(pnode, command, data); });
            return true;
        }

        if (CNode *node = netConnMgr->QueryNode(nodeID))
        {
            netConnMgr->PushMessage(node, command, data);
            return true;
        }
    }
    return false;
}

bool CNetComponent::BroadcastTransaction(uint256 txHash)
{
    if (netConnMgr)
    {
        CInv inv(MSG_TX, txHash);
        netConnMgr->ForEachNode([&inv](CNode *pnode)
                                {
                                    pnode->PushInventory(inv);
                                });
        return true;
    }
    return false;
}

bool CNetComponent::RelayCmpctBlock(const CBlockIndex *pindex, void *pcmpctblock, bool fWitnessEnabled)
{
    return peerLogic && peerLogic->RelayCmpctBlock(pindex, pcmpctblock, fWitnessEnabled);
}

bool CNetComponent::AskForTransaction(int64_t nodeID, uint256 txHash, int flags)
{
    if (netConnMgr)
    {
        if (CNode *node = netConnMgr->QueryNode(nodeID))
        {
            node->AskFor(CInv(MSG_TX | flags, txHash));
            return true;
        }
    }

    return false;
}

bool CNetComponent::AddTxInventoryKnown(int64_t nodeID, uint256 txHash, int flags)
{
    if (netConnMgr)
    {
        if (CNode *node = netConnMgr->QueryNode(nodeID))
        {
            node->AddInventoryKnown(CInv(MSG_TX | flags, txHash));
            return true;
        }
    }

    return false;
}

bool CNetComponent::MisbehaveNode(int64_t nodeID, int num)
{
    if (netConnMgr)
    {
        Misbehaving(nodeID, num);
        return true;
    }
    return false;
}

bool CNetComponent::OutboundTargetReached(bool historicalBlockServingLimit)
{
    if (netConnMgr)
    {
        return netConnMgr->OutboundTargetReached(historicalBlockServingLimit);
    }
    return true;
}

int CNetComponent::GetNodeCount(int flags)
{
    if (netConnMgr)
    {
        return (int)netConnMgr->GetNodeCount((CConnman::NumConnections)flags);
    }
    return 0;
}

void CNetComponent::UpdateBlockAvailability(int64_t nodeid, uint256 hash)
{
    if (netConnMgr && peerLogic)
    {
        UpdateNodeBlockAvailability(nodeid, hash);
    }
}

int CNetComponent::GetInFlightBlockCount()
{
    if (netConnMgr && peerLogic)
    {
        return ::GetInFlightBlockCount();
    }
    return 0;
}

bool CNetComponent::DoseBlockInFlight(uint256 hash)
{
    if (netConnMgr && peerLogic)
    {
        return ::DoseBlockInFlight(hash);
    }
    return false;
}

bool CNetComponent::MarkBlockInFlight(int64_t nodeid, uint256 hash, const CBlockIndex *pindex)
{
    if (netConnMgr && peerLogic)
    {
        return ::MarkNodeBlockInFlight(nodeid, hash, pindex);
    }
    return false;
}

