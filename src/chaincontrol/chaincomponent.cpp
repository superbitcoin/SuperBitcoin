#include <iostream>
#include "chaincomponent.h"
#include "sbtccore/streams.h"
#include "interface/inetcomponent.h"
#include "utils/net/netmessagehelper.h"
#include "sbtccore/block/validation.h"
#include "utils/reverse_iterator.h"
#include "interface/imempoolcomponent.h"
#include "framework/warnings.h"
#include "utils/util.h"
#include "sbtccore/block/merkle.h"
#include "sbtccore/checkqueue.h"
#include "sbtccore/block/undo.h"
#include "utils/merkleblock.h"
#include "framework/base.hpp"
#include "interface/ibasecomponent.h"
#include "eventmanager/eventmanager.h"

static int64_t nTimeCheck = 0;
static int64_t nTimeForks = 0;
static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;
static int64_t nTimeTotal = 0;
static CCheckQueue<CScriptCheck> scriptcheckqueue(128);

void CChainCommonent::ThreadScriptCheck()
{
    RenameThread("bitcoin-scriptch");
    scriptcheckqueue.Thread();
}

CChainCommonent::CChainCommonent()
{
}

CChainCommonent::~CChainCommonent()
{
}

bool CChainCommonent::ComponentInitialize()
{
    std::cout << "initialize chain component \n";

    GET_BASE_INTERFACE(ifBaseObj);
    assert(ifBaseObj != nullptr);

    ifBaseObj->GetEventManager()->RegisterEventHandler(EID_NODE_DISCONNECTED, this, &CChainCommonent::OnNodeDisconnected);

    return true;
}

bool CChainCommonent::ComponentStartup()
{
    std::cout << "startup chain component \n";
    return true;
}

bool CChainCommonent::ComponentShutdown()
{
    std::cout << "shutdown chain component \n";
    return true;
}

bool CChainCommonent::IsImporting() const
{
    //TODO:
    return fImporting;
}

bool CChainCommonent::IsReindexing() const
{
    //TODO:
    return fReindex;
}

bool CChainCommonent::IsInitialBlockDownload() const
{
    //TODO:
    return IsInitialBlockDownload();
}

bool CChainCommonent::DoesBlockExist(uint256 hash)
{
    //TODO:
    return cIndexManager.GetBlockIndex(hash);
}

int CChainCommonent::GetActiveChainHeight()
{
    //TODO:
    return cIndexManager.GetChain().Height();
}

bool CChainCommonent::GetActiveChainTipHash(uint256 &tipHash)
{
    if (CBlockIndex *tip = cIndexManager.GetChain().Tip())
    {
        tipHash = tip->GetBlockHash();
        return true;
    }

    return false;
}

void CChainCommonent::OnNodeDisconnected(int64_t nodeID, bool /*bInBound*/, int /*disconnectReason*/)
{
    LOCK(cs_xnodeGuard);
    m_nodeCheckPointKnown.erase(nodeID);
}

bool CChainCommonent::NetRequestCheckPoint(ExNode *xnode, int height)
{
    assert(xnode != nullptr);

    std::vector<Checkpoints::CCheckData> vSendData;
    std::vector<Checkpoints::CCheckData> vnHeight;
    Checkpoints::GetCheckpointByHeight(height, vnHeight);
    {
        LOCK(cs_xnodeGuard);
        std::set<int> &checkpointKnown = m_nodeCheckPointKnown[xnode->nodeID];
        for (const auto &point : vnHeight)
        {
            if (checkpointKnown.count(point.getHeight()) == 0)
            {
                checkpointKnown.insert(point.getHeight());
                vSendData.push_back(point);
            }
        }
    }

    if (!vSendData.empty())
    {
        SendNetMessage(xnode->nodeID, NetMsgType::CHECKPOINT, xnode->sendVersion, 0, vSendData);
    }

    return true;
}

bool CChainCommonent::NetReceiveCheckPoint(ExNode *xnode, CDataStream &stream)
{
    assert(xnode != nullptr);

    LogPrint(BCLog::NET, "enter checkpoint\n");
    LogPrint(BCLog::BENCH, "receive check block list====\n");

    std::vector<Checkpoints::CCheckData> vdata;
    stream >> vdata;

    Checkpoints::CCheckPointDB cCheckPointDB;
    std::vector<int> toInsertCheckpoints;
    std::vector<Checkpoints::CCheckData> vIndex;
    const CChainParams &chainparams = appbase::app().GetChainParams();
    for (const auto &point : vdata)
    {
        if (point.CheckSignature(chainparams.GetCheckPointPKey()))
        {
            if (!cCheckPointDB.ExistCheckpoint(point.getHeight()))
            {
                cCheckPointDB.WriteCheckpoint(point.getHeight(), point);
                /*
                 * add the check point to chainparams
                 */
                chainparams.AddCheckPoint(point.getHeight(), point.getHash());
                toInsertCheckpoints.push_back(point.getHeight());
                vIndex.push_back(point);
            }
        } else
        {
            LogPrint(BCLog::NET, "check point signature check failed \n");
            break;
        }
        LogPrint(BCLog::BENCH, "block height=%d, block hash=%s\n", point.getHeight(), point.getHash().ToString());
    }

    if (!toInsertCheckpoints.empty())
    {
        LOCK(cs_xnodeGuard);
        std::set<int> &checkpointKnown = m_nodeCheckPointKnown[xnode->nodeID];
        checkpointKnown.insert(toInsertCheckpoints.begin(), toInsertCheckpoints.end());
    }

    if (vIndex.size() > 0)
    {
        CValidationState state;
        if (!CheckActiveChain(state, chainparams))
        {
            LogPrint(BCLog::NET, "CheckActiveChain error when receive  checkpoint\n");
        }
    }

    //broadcast the check point if it is necessary
    if (vIndex.size() == 1 && vIndex.size() == vdata.size())
    {
        SendNetMessage(-1, NetMsgType::CHECKPOINT, xnode->sendVersion, 0, vdata);
    }

    return true;
}

bool CChainCommonent::NetRequestBlocks(ExNode *xnode, CDataStream &stream, std::vector<uint256> &blockHashes)
{
    assert(xnode != nullptr);

    CBlockLocator locator;
    uint256 hashStop;
    stream >> locator >> hashStop;

    // We might have announced the currently-being-connected tip using a
    // compact block, which resulted in the peer sending a getblocks
    // request, which we would otherwise respond to without the new block.
    // To avoid this situation we simply verify that we are on our best
    // known chain now. This is super overkill, but we handle it better
    // for getheaders requests, and there are no known nodes which support
    // compact blocks but still use getblocks to request blocks.
    //{
    //    std::shared_ptr<const CBlock> a_recent_block;
    //    {
    //        LOCK(cs_most_recent_block);
    //        a_recent_block = most_recent_block;
    //    }
    //    CValidationState dummy;
    //    ActivateBestChain(dummy, Params(), a_recent_block);
    //}

    LOCK(cs_main);

    // Find the last block the caller has in the main chain
    const CBlockIndex *pindex = FindForkInGlobalIndex(chainActive, locator);

    // Send the rest of the chain
    if (pindex)
        pindex = cIndexManager.GetChain().Next(pindex);

    int nLimit = 500;
    LogPrint(BCLog::NET, "getblocks %d to %s limit %d from peer=%d\n", (pindex ? pindex->nHeight : -1),
             hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, xnode->nodeID);

    const CChainParams &chainparams = appbase::app().GetChainParams();
    for (; pindex; pindex = cIndexManager.GetChain().Next(pindex))
    {
        if (pindex->GetBlockHash() == hashStop)
        {
            LogPrint(BCLog::NET, "  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
            break;
        }
        // If pruning, don't inv blocks unless we have on disk and are likely to still have
        // for some reasonable time window (1 hour) that block relay might require.
        const int nPrunedBlocksLikelyToHave =
                MIN_BLOCKS_TO_KEEP - 3600 / chainparams.GetConsensus().nPowTargetSpacing;
        if (fPruneMode && (!(pindex->nStatus & BLOCK_HAVE_DATA) ||
                           pindex->nHeight <= Tip()->nHeight - nPrunedBlocksLikelyToHave))
        {
            LogPrint(BCLog::NET, " getblocks stopping, pruned or too old block at %d %s\n", pindex->nHeight,
                     pindex->GetBlockHash().ToString());
            break;
        }
        blockHashes.emplace_back(pindex->GetBlockHash());
        if (--nLimit <= 0)
        {
            // When this block is requested, we'll send an inv that'll
            // trigger the peer to getblocks the next batch of inventory.
            LogPrint(BCLog::NET, "  getblocks stopping at limit %d %s\n", pindex->nHeight,
                     pindex->GetBlockHash().ToString());
            break;
        }
    }
    return true;
}

bool CChainCommonent::NetRequestHeaders(ExNode *xnode, CDataStream &stream)
{
    assert(xnode != nullptr);

    if (IsInitialBlockDownload() && !IsFlagsBitOn(xnode->flags, NF_WHITELIST))
    {
        LogPrint(BCLog::NET, "Ignoring getheaders from peer=%d because node is in initial block download\n",
                 xnode->nodeID);
        return true;
    }

    CBlockLocator locator;
    uint256 hashStop;
    stream >> locator >> hashStop;

    const CBlockIndex *pindex = nullptr;
    if (locator.IsNull())
    {
        // If locator is null, return the hashStop block
        pindex = cIndexManager.GetBlockIndex(hashStop);
        if (!pindex)
        {
            return true;
        }
    } else
    {
        // Find the last block the caller has in the main chain
        pindex = FindForkInGlobalIndex(cIndexManager.GetChain(), locator);
        if (pindex)
            pindex = cIndexManager.GetChain().Next(pindex);
    }

    // we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at the end
    std::vector<CBlock> vHeaders;
    int nLimit = MAX_HEADERS_RESULTS;

    LogPrint(BCLog::NET, "getheaders %d to %s from peer=%d\n", (pindex ? pindex->nHeight : -1),
             hashStop.IsNull() ? "end" : hashStop.ToString(), xnode->nodeID);

    for (; pindex; pindex = cIndexManager.GetChain().Next(pindex))
    {
        vHeaders.push_back(pindex->GetBlockHeader());
        if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
            break;
    }

    return SendNetMessage(xnode->nodeID, NetMsgType::HEADERS, xnode->sendVersion, 0, vHeaders);
}

bool CChainCommonent::NetReceiveHeaders(ExNode *xnode, CDataStream &stream)
{
    assert(xnode != nullptr);

    std::vector<CBlockHeader> headers;

    // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
    unsigned int nCount = ReadCompactSize(stream);
    if (nCount > MAX_HEADERS_RESULTS)
    {
        LOCK(cs_main);
        xnode->nMisbehavior = 20;
        return error("headers message size = %u", nCount);
    }

    headers.resize(nCount);
    for (unsigned int n = 0; n < nCount; n++)
    {
        stream >> headers[n];
        ReadCompactSize(stream); // ignore tx count; assume it is 0.
    }

    return NetReceiveHeaders(xnode, headers);
}

bool CChainCommonent::NetReceiveHeaders(ExNode *xnode, const std::vector<CBlockHeader> &headers)
{
    assert(xnode != nullptr);

    size_t nCount = headers.size();
    if (nCount == 0)
        return true;

    // If this looks like it could be a block announcement (nCount <
    // MAX_BLOCKS_TO_ANNOUNCE), use special logic for handling headers that
    // don't connect:
    // - Send a getheaders message in response to try to connect the chain.
    // - The peer can send up to MAX_UNCONNECTING_HEADERS in a row that
    //   don't connect before giving DoS points
    // - Once a headers message is received that is valid and does connect,
    //   nUnconnectingHeaders gets reset back to 0.
    if (!DoesBlockExist(headers[0].hashPrevBlock) && nCount < MAX_BLOCKS_TO_ANNOUNCE)
    {
        xnode->nUnconnectingHeaders++;
        SendNetMessage(xnode->nodeID, NetMsgType::GETHEADERS, xnode->sendVersion, 0,
                       cIndexManager.GetChain().GetLocator(pindexBestHeader), uint256());

        LogPrint(BCLog::NET,
                 "received header %s: missing prev block %s, sending getheaders (%d) to end (peer=%d, nUnconnectingHeaders=%d)\n",
                 headers[0].GetHash().ToString(),
                 headers[0].hashPrevBlock.ToString(),
                 pindexBestHeader->nHeight,
                 xnode->nodeID, xnode->nUnconnectingHeaders);

        // Set hashLastUnknownBlock for this peer, so that if we
        // eventually get the headers - even from a different peer -
        // we can use this peer to download.
        ///UpdateBlockAvailability(pfrom->GetId(), headers.back().GetHash());

        if (xnode->nUnconnectingHeaders % MAX_UNCONNECTING_HEADERS == 0)
        {
            xnode->nMisbehavior += 20;
        }
        return true;
    }

    uint256 hashLastBlock;
    for (const CBlockHeader &header : headers)
    {
        if (!hashLastBlock.IsNull() && header.hashPrevBlock != hashLastBlock)
        {
            xnode->nMisbehavior += 20;
            return error("non-continuous headers sequence");
        }
        hashLastBlock = header.GetHash();
    }

    // If we don't have the last header, then they'll have given us
    // something new (if these headers are valid).
    bool received_new_header = !DoesBlockExist(hashLastBlock);

    CValidationState state;
    CBlockHeader first_invalid_header;
    const CBlockIndex *pindexLast = nullptr;
    if (!ProcessNewBlockHeaders(headers, state, appbase::app().GetChainParams(), &pindexLast, &first_invalid_header))
    {
        int nDoS;
        if (state.IsInvalid(nDoS))
        {
            if (nDoS > 0)
            {
                xnode->nMisbehavior += nDoS;
            }
            if (IsFlagsBitOn(xnode->flags, NF_OUTBOUND) &&
                !IsFlagsBitOn(xnode->flags, NF_MANUALCONN) &&
                DoesBlockExist(first_invalid_header.GetHash()))
            {
                // Goal: don't allow outbound peers to use up our outbound
                // connection slots if they are on incompatible chains.
                //
                // We ask the caller to set punish_invalid appropriately based
                // on the peer and the method of header delivery (compact
                // blocks are allowed to be invalid in some circumstances,
                // under BIP 152).
                // Here, we try to detect the narrow situation that we have a
                // valid block header (ie it was valid at the time the header
                // was received, and hence stored in mapBlockIndex) but know the
                // block is invalid, and that a peer has announced that same
                // block as being on its active chain.
                // Disconnect the peer in such a situation.
                //
                // Note: if the header that is invalid was not accepted to our
                // mapBlockIndex at all, that may also be grounds for
                // disconnecting the peer, as the chain they are on is likely
                // to be incompatible. However, there is a circumstance where
                // that does not hold: if the header's timestamp is more than
                // 2 hours ahead of our current time. In that case, the header
                // may become valid in the future, and we don't want to
                // disconnect a peer merely for serving us one too-far-ahead
                // block header, to prevent an attacker from splitting the
                // network by mining a block right at the 2 hour boundary.
                //
                // TODO: update the DoS logic (or, rather, rewrite the
                // DoS-interface between validation and net_processing) so that
                // the interface is cleaner, and so that we disconnect on all the
                // reasons that a peer's headers chain is incompatible
                // with ours (eg block->nVersion softforks, MTP violations,
                // etc), and not just the duplicate-invalid case.
                SetFlagsBit(xnode->retFlags, NF_DISCONNECT);
            }
            return error("invalid header received");
        }
    }

    {
        if (xnode->nUnconnectingHeaders > 0)
        {
            LogPrint(BCLog::NET, "peer=%d: resetting nUnconnectingHeaders (%d -> 0)\n", xnode->nodeID,
                     xnode->nUnconnectingHeaders);
        }
        xnode->nUnconnectingHeaders = 0;

        //assert(pindexLast);
        //UpdateBlockAvailability(pfrom->GetId(), pindexLast->GetBlockHash());

        // From here, pindexBestKnownBlock should be guaranteed to be non-null,
        // because it is set in UpdateBlockAvailability. Some nullptr checks
        // are still present, however, as belt-and-suspenders.

        //if (received_new_header && pindexLast->nChainWork > chainActive.Tip()->nChainWork)
        //{
        //    nodestate->m_last_block_announcement = GetTime();
        //}

        if (nCount == MAX_HEADERS_RESULTS)
        {
            // Headers message had its maximum size; the peer may have more headers.
            // TODO: optimize: if pindexLast is an ancestor of chainActive.Tip or pindexBestHeader, continue
            // from there instead.
            LogPrint(BCLog::NET, "more getheaders (%d) to end to peer=%d (startheight:%d)\n", pindexLast->nHeight,
                     xnode->nodeID, xnode->startHeight);

            SendNetMessage(xnode->nodeID, NetMsgType::GETHEADERS, xnode->sendVersion, 0,
                           cIndexManager.GetChain().GetLocator(pindexLast), uint256());
        }

        bool fCanDirectFetch = Tip()->GetBlockTime() > (GetAdjustedTime() -
                                                        appbase::app().GetChainParams().GetConsensus().nPowTargetSpacing *
                                                        20);

        // If this set of headers is valid and ends in a block with at least as
        // much work as our tip, download as much as possible.
        if (fCanDirectFetch && pindexLast->IsValid(BLOCK_VALID_TREE) &&
            Tip()->nChainWork <= pindexLast->nChainWork)
        {
            std::vector<const CBlockIndex *> vToFetch;
            const CBlockIndex *pindexWalk = pindexLast;
            // Calculate all the blocks we'd need to switch to pindexLast, up to a limit.
            while (pindexWalk && !cIndexManager.GetChain().Contains(pindexWalk) &&
                   vToFetch.size() <= MAX_BLOCKS_IN_TRANSIT_PER_PEER)
            {
                if (!(pindexWalk->nStatus & BLOCK_HAVE_DATA) &&
                    //!mapBlocksInFlight.count(pindexWalk->GetBlockHash()) &&
                    (!IsWitnessEnabled(pindexWalk->pprev, appbase::app().GetChainParams().GetConsensus()) ||
                     IsFlagsBitOn(xnode->flags, NF_WITNESS)))
                {
                    // We don't have this block, and it's not yet in flight.
                    vToFetch.push_back(pindexWalk);
                }
                pindexWalk = pindexWalk->pprev;
            }
            // If pindexWalk still isn't on our main chain, we're looking at a
            // very large reorg at a time we think we're close to caught up to
            // the main chain -- this shouldn't really happen.  Bail out on the
            // direct fetch and rely on parallel download instead.
            if (!cIndexManager.GetChain().Contains(pindexWalk))
            {
                LogPrint(BCLog::NET, "Large reorg, won't direct fetch to %s (%d)\n",
                         pindexLast->GetBlockHash().ToString(),
                         pindexLast->nHeight);
            } else
            {
                uint32_t nFetchFlags = 0;
                if (IsFlagsBitOn(xnode->nLocalServices, NODE_WITNESS) &&
                    IsFlagsBitOn(xnode->flags, NF_WITNESS))
                    nFetchFlags = MSG_WITNESS_FLAG;

                std::vector<CInv> vGetData;
                // Download as much as possible, from earliest to latest.
                for (const CBlockIndex *pindex : reverse_iterate(vToFetch))
                {
                    if (xnode->nBlocksInFlight >= MAX_BLOCKS_IN_TRANSIT_PER_PEER)
                    {
                        // Can't download any more from this peer
                        break;
                    }

                    vGetData.push_back(CInv(MSG_BLOCK | nFetchFlags, pindex->GetBlockHash()));
                    // MarkBlockAsInFlight(pfrom->GetId(), pindex->GetBlockHash(), pindex);
                    LogPrint(BCLog::NET, "Requesting block %s from  peer=%d\n",
                             pindex->GetBlockHash().ToString(), xnode->nodeID);
                }
                if (vGetData.size() > 1)
                {
                    LogPrint(BCLog::NET, "Downloading blocks toward %s (%d) via headers direct fetch\n",
                             pindexLast->GetBlockHash().ToString(), pindexLast->nHeight);
                }
                if (vGetData.size() > 0)
                {
                    if (IsFlagsBitOn(xnode->flags, NF_DESIREDCMPCTVERSION) && vGetData.size() == 1 &&
                        //mapBlocksInFlight.size() == 1 &&
                        pindexLast->pprev->IsValid(BLOCK_VALID_CHAIN))
                    {
                        // In any case, we want to download using a compact block, not a regular one
                        vGetData[0] = CInv(MSG_CMPCT_BLOCK, vGetData[0].hash);
                    }
                    SendNetMessage(xnode->nodeID, NetMsgType::GETDATA, xnode->sendVersion, 0, vGetData);
                }
            }
        }
        // If we're in IBD, we want outbound peers that will serve us a useful
        // chain. Disconnect peers that are on chains with insufficient work.
        //        if (IsInitialBlockDownload() && nCount != MAX_HEADERS_RESULTS)
        //        {
        //            // When nCount < MAX_HEADERS_RESULTS, we know we have no more
        //            // headers to fetch from this peer.
        //            if (nodestate->pindexBestKnownBlock && nodestate->pindexBestKnownBlock->nChainWork < nMinimumChainWork)
        //            {
        //                // This peer has too little work on their headers chain to help
        //                // us sync -- disconnect if using an outbound slot (unless
        //                // whitelisted or addnode).
        //                // Note: We compare their tip to nMinimumChainWork (rather than
        //                // chainActive.Tip()) because we won't start block download
        //                // until we have a headers chain that has at least
        //                // nMinimumChainWork, even if a peer has a chain past our tip,
        //                // as an anti-DoS measure.
        //                if (IsOutboundDisconnectionCandidate(pfrom))
        //                {
        //                    LogPrintf("Disconnecting outbound peer %d -- headers chain has insufficient work\n",
        //                              xnode->nodeID);
        //                    SetFlagsBit(xnode->retFlags, NF_DISCONNECT);
        //                }
        //            }
        //        }
        //
        //        if (!pfrom->fDisconnect && IsOutboundDisconnectionCandidate(pfrom) &&
        //            nodestate->pindexBestKnownBlock != nullptr)
        //        {
        //            // If this is an outbound peer, check to see if we should protect
        //            // it from the bad/lagging chain logic.
        //            if (g_outbound_peers_with_protect_from_disconnect < MAX_OUTBOUND_PEERS_TO_PROTECT_FROM_DISCONNECT &&
        //                nodestate->pindexBestKnownBlock->nChainWork >= chainActive.Tip()->nChainWork &&
        //                !nodestate->m_chain_sync.m_protect)
        //            {
        //                LogPrint(BCLog::NET, "Protecting outbound peer=%d from eviction\n", pfrom->GetId());
        //                nodestate->m_chain_sync.m_protect = true;
        //                ++g_outbound_peers_with_protect_from_disconnect;
        //            }
        //        }
    }

    return true;
}

bool CChainCommonent::NetRequestBlockData(ExNode *xnode, uint256 blockHash, int blockType)
{
    assert(xnode != nullptr);

    GET_NET_INTERFACE(ifNetObj);
    assert(ifNetObj != nullptr);

    bool isOK = false;
    const Consensus::Params &consensusParams = appbase::app().GetChainParams().GetConsensus();

    //    std::shared_ptr<const CBlock> a_recent_block;
    //    std::shared_ptr<const CBlockHeaderAndShortTxIDs> a_recent_compact_block;
    //    bool fWitnessesPresentInARecentCompactBlock;
    //    {
    //        LOCK(cs_most_recent_block);
    //        a_recent_block = most_recent_block;
    //        a_recent_compact_block = most_recent_compact_block;
    //        fWitnessesPresentInARecentCompactBlock = fWitnessesPresentInMostRecentCompactBlock;
    //    }

    CBlockIndex *bi = cIndexManager.GetBlockIndex(blockHash);
    if (bi != nullptr)
    {
        if (bi->nChainTx && !bi->IsValid(BLOCK_VALID_SCRIPTS) && bi->IsValid(BLOCK_VALID_TREE))
        {
            // If we have the block and all of its parents, but have not yet validated it,
            // we might be in the middle of connecting it (ie in the unlock of cs_main
            // before ActivateBestChain but after AcceptBlock).
            // In this case, we need to run ActivateBestChain prior to checking the relay
            // conditions below.
            // CValidationState dummy;
            // ActivateBestChain(dummy, Params(), a_recent_block);
        }

        if (cIndexManager.GetChain().Contains(bi))
        {
            isOK = true;
        } else
        {
            static const int nOneMonth = 30 * 24 * 60 * 60;
            // To prevent fingerprinting attacks, only send blocks outside of the active
            // chain if they are valid, and no more than a month older (both in time, and in
            // best equivalent proof of work) than the best header chain we know about.
            isOK = bi->IsValid(BLOCK_VALID_SCRIPTS) && (pindexBestHeader != nullptr) &&
                   (pindexBestHeader->GetBlockTime() - bi->GetBlockTime() < nOneMonth) &&
                   (GetBlockProofEquivalentTime(*pindexBestHeader, *bi, *pindexBestHeader,
                                                consensusParams) < nOneMonth);
            if (!isOK)
            {
                LogPrintf("%s: ignoring request from peer=%i for old block that isn't in the main chain\n",
                          __func__, xnode->nodeID);
            }
        }
    }
    // disconnect node in case we have reached the outbound limit for serving historical blocks
    // never disconnect whitelisted nodes
    static const int nOneWeek = 7 * 24 * 60 * 60; // assume > 1 week = historical
    if (isOK && ifNetObj->OutboundTargetReached(true) && (((pindexBestHeader != nullptr) &&
                                                           (pindexBestHeader->GetBlockTime() -
                                                            bi->GetBlockTime() > nOneWeek)) ||
                                                          blockType == MSG_FILTERED_BLOCK) &&
        !IsFlagsBitOn(xnode->flags, NF_WHITELIST))
    {
        LogPrint(BCLog::NET, "historical block serving limit reached, disconnect peer=%d\n",
                 xnode->nodeID);

        //disconnect node
        SetFlagsBit(xnode->retFlags, NF_DISCONNECT);
        isOK = false;
    }
    // Pruned nodes may have deleted the block, so check whether
    // it's available before trying to send.
    if (isOK && (bi->nStatus & BLOCK_HAVE_DATA))
    {
        std::shared_ptr<const CBlock> pblock;
        //        if (a_recent_block && a_recent_block->GetHash() == (*mi).second->GetBlockHash())
        //        {
        //            pblock = a_recent_block;
        //        } else
        {
            // Send block from disk
            std::shared_ptr<CBlock> pblockRead = std::make_shared<CBlock>();
            if (!ReadBlockFromDisk(*pblockRead, bi, consensusParams))
                assert(!"cannot load block from disk");
            pblock = pblockRead;
        }
        if (blockType == MSG_BLOCK)
        {
            SendNetMessage(xnode->nodeID, NetMsgType::BLOCK, xnode->sendVersion, SERIALIZE_TRANSACTION_NO_WITNESS,
                           *pblock);
        } else if (blockType == MSG_WITNESS_BLOCK)
        {
            SendNetMessage(xnode->nodeID, NetMsgType::BLOCK, xnode->sendVersion, 0, *pblock);
        } else if (blockType == MSG_FILTERED_BLOCK)
        {
            bool sendMerkleBlock = false;
            CMerkleBlock merkleBlock;
            //            {
            //                LOCK(pfrom->cs_filter);
            //                if (pfrom->pfilter)
            //                {
            //                    sendMerkleBlock = true;
            //                    merkleBlock = CMerkleBlock(*pblock, *pfrom->pfilter);
            //                }
            //            }
            if (sendMerkleBlock)
            {
                SendNetMessage(xnode->nodeID, NetMsgType::MERKLEBLOCK, xnode->sendVersion, 0, merkleBlock);
                // CMerkleBlock just contains hashes, so also push any transactions in the block the client did not see
                // This avoids hurting performance by pointlessly requiring a round-trip
                // Note that there is currently no way for a node to request any single transactions we didn't send here -
                // they must either disconnect and retry or request the full block.
                // Thus, the protocol spec specified allows for us to provide duplicate txn here,
                // however we MUST always provide at least what the remote peer needs
                typedef std::pair<unsigned int, uint256> PairType;
                for (PairType &pair : merkleBlock.vMatchedTxn)
                    SendNetMessage(xnode->nodeID, NetMsgType::TX, xnode->sendVersion, SERIALIZE_TRANSACTION_NO_WITNESS,
                                   *pblock->vtx[pair.first]);
            }
            // else
            // no response
        } else if (blockType == MSG_CMPCT_BLOCK)
        {
            //            // If a peer is asking for old blocks, we're almost guaranteed
            //            // they won't have a useful mempool to match against a compact block,
            //            // and we don't feel like constructing the object for them, so
            //            // instead we respond with the full, non-compact block.
            //            bool fPeerWantsWitness = IsFlagsBitOn(xnode->flags, NF_WANTCMPCTWITNESS);
            //            int nSendFlags = fPeerWantsWitness ? 0 : SERIALIZE_TRANSACTION_NO_WITNESS;
            //            if (CanDirectFetch(consensusParams) &&
            //                bi->nHeight >= cIndexManager.GetChain().Height() - MAX_CMPCTBLOCK_DEPTH)
            //            {
            //                if ((fPeerWantsWitness || !fWitnessesPresentInARecentCompactBlock) &&
            //                    a_recent_compact_block &&
            //                    a_recent_compact_block->header.GetHash() == bi->GetBlockHash())
            //                {
            //                    SendNetMessage(xnode->nodeID, NetMsgType::MERKLEBLOCK, xnode->sendVersion, nSendFlags, *a_recent_compact_block);
            //                }
            //                else
            //                {
            //                    CBlockHeaderAndShortTxIDs cmpctblock(*pblock, fPeerWantsWitness);
            //                    SendNetMessage(xnode->nodeID, NetMsgType::CMPCTBLOCK, xnode->sendVersion, nSendFlags, cmpctblock);
            //                }
            //            }
            //            else
            //            {
            //                SendNetMessage(xnode->nodeID, NetMsgType::BLOCK, xnode->sendVersion, nSendFlags, *pblock);
            //            }
        }
    }
    return isOK;
}

bool CChainCommonent::NetReceiveBlockData(ExNode *xnode, CDataStream &stream, uint256 &blockHash)
{
    assert(xnode != nullptr);

    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    stream >> *
            pblock;

    LogPrint(BCLog::NET, "received block %s peer=%d\n", pblock->GetHash().ToString(), xnode->nodeID);

    bool forceProcessing = false;
    const uint256 hash(pblock->GetHash());
    //    {
    //        LOCK(cs_main);
    //        // Also always process if we requested the block explicitly, as we may
    //        // need it even though it is not a candidate for a new best tip.
    //        forceProcessing |= MarkBlockAsReceived(hash);
    //        // mapBlockSource is only used for sending reject messages and DoS scores,
    //        // so the race between here and cs_main in ProcessNewBlock is fine.
    //        mapBlockSource.emplace(hash, std::make_pair(pfrom->GetId(), true));
    //    }

    bool fNewBlock = false;

    ProcessNewBlock(Params(), pblock, forceProcessing, &fNewBlock

    );
    if (fNewBlock)
    {
        SetFlagsBit(xnode
                            ->retFlags, NF_NEWBLOCK);
    }

    return true;
}

CBlockIndex *CChainCommonent::Tip()
{
    return cIndexManager.GetChain().Tip();
}

void CChainCommonent::SetTip(CBlockIndex *pIndexTip)
{
    cIndexManager.GetChain().SetTip(pIndexTip);
}

bool CChainCommonent::ReplayBlocks()
{
    const CChainParams &params = appbase::app().GetChainParams();

    CCoinsView *view(cViewManager.GetCoinViewDB());
    CCoinsViewCache cache(view);

    std::vector<uint256> vecHashHeads = view->GetHeadBlocks();
    if (vecHashHeads.empty())
        return true; // We're already in a consistent state.
    if (vecHashHeads.size() != 2)
        return error("ReplayBlocks(): unknown inconsistent state");

    LogPrintf("Replaying blocks\n");

    const CBlockIndex *pIndexOld = nullptr;  // Old tip during the interrupted flush.
    const CBlockIndex *pIndexNew;            // New tip during the interrupted flush.
    const CBlockIndex *pIndexFork = nullptr; // Latest block common to both the old and the new tip.

    pIndexNew = cIndexManager.GetBlockIndex(vecHashHeads[0]);
    if (!pIndexNew)
    {
        return false;
    }

    if (!vecHashHeads[1].IsNull())
    {
        pIndexOld = cIndexManager.GetBlockIndex(vecHashHeads[1]);
        if (!pIndexOld)
        {
            return false;
        }
        pIndexFork = cIndexManager.LastCommonAncestor(pIndexNew, pIndexOld);
        assert(pIndexFork != nullptr);
    }

    // Rollback along the old branch.
    while (pIndexOld != pIndexFork)
    {
        if (pIndexOld->nHeight > 0) // Never disconnect the genesis block.
        {
            CBlock block;
            if (!ReadBlockFromDisk(block, pIndexOld, params.GetConsensus()))
            {
                return error("RollbackBlock(): ReadBlockFromDisk() failed at %d, hash=%s", pIndexOld->nHeight,
                             pIndexOld->GetBlockHash().ToString());
            }

            LogPrintf("Rolling back %s (%i)\n", pIndexOld->GetBlockHash().ToString(), pIndexOld->nHeight);

            DisconnectResult res = cViewManager.DisconnectBlock(block, pIndexOld, cache);
            if (res == DISCONNECT_FAILED)
            {
                return error("RollbackBlock(): DisconnectBlock failed at %d, hash=%s", pIndexOld->nHeight,
                             pIndexOld->GetBlockHash().ToString());
            }
            // If DISCONNECT_UNCLEAN is returned, it means a non-existing UTXO was deleted, or an existing UTXO was
            // overwritten. It corresponds to cases where the block-to-be-disconnect never had all its operations
            // applied to the UTXO set. However, as both writing a UTXO and deleting a UTXO are idempotent operations,
            // the result is still a version of the UTXO set with the effects of that block undone.
        }
        pIndexOld = pIndexOld->pprev;
    }

    // Roll forward from the forking point to the new tip.
    int iForkHeight = pIndexFork ? pIndexFork->nHeight : 0;
    for (int nHeight = iForkHeight + 1; nHeight <= pIndexNew->nHeight; ++nHeight)
    {
        const CBlockIndex *pIndex = pIndexNew->GetAncestor(nHeight);
        LogPrintf("Rolling forward %s (%i)\n", pIndex->GetBlockHash().ToString(), nHeight);
        CBlock block;
        if (!ReadBlockFromDisk(block, pIndex, params.GetConsensus()))
        {
            return error("ReplayBlock(): ReadBlockFromDisk failed at %d, hash=%s", pIndex->nHeight,
                         pIndex->GetBlockHash().ToString());
        }
        if (!cViewManager.ConnectBlock(block, pIndex, cache))
            return false;
    }

    cache.SetBestBlock(pIndexNew->GetBlockHash());
    cache.Flush();

    return true;
}

bool CChainCommonent::NeedFullFlush(FlushStateMode mode)
{
    return true;
}

bool CChainCommonent::FlushStateToDisk(CValidationState &state, FlushStateMode mode, const CChainParams &chainparams)
{
    int iLastBlockFile = cIndexManager.GetLastBlockFile();
    int iSize = cIndexManager.GetBlockFileInfo()[iLastBlockFile].nSize;
    int iUndoSize = cIndexManager.GetBlockFileInfo()[iLastBlockFile].nUndoSize;

    // block file flush
    cFileManager.Flush(iLastBlockFile, iSize, iUndoSize);

    // block index flush
    if (!cIndexManager.Flush())
    {
        assert(0);
        return false;
    }

    // view flush
    if (!cViewManager.Flush())
    {
        return false;
    }

    return true;
}

bool CChainCommonent::IsInitialBlockDownload()
{
    // Once this function has returned false, it must remain false.
    static std::atomic<bool> latchToFalse{false};
    // Optimization: pre-test latch before taking the lock.
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;

    LOCK(cs);
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;
    if (fImporting || fReindex)
        return true;
    if (Tip() == nullptr)
        return true;
    if (Tip()->nChainWork < nMinimumChainWork)
        return true;
    if (Tip()->GetBlockTime() < (GetTime() - nMaxTipAge))
        return true;
    LogPrintf("Leaving InitialBlockDownload (latching to false)\n");
    latchToFalse.store(true, std::memory_order_relaxed);
    return false;
}

void CChainCommonent::DoWarning(const std::string &strWarning)
{
    static bool fWarned = false;
    SetMiscWarning(strWarning);
    if (!fWarned)
    {
        AlertNotify(strWarning);
        fWarned = true;
    }
}

/** Update chainActive and related internal data structures. */
void CChainCommonent::UpdateTip(CBlockIndex *pindexNew, const CChainParams &chainParams)
{
    CChain chainActive = cIndexManager.GetChain();

    chainActive.SetTip(pindexNew);

    ITxMempoolComponent *txmempool = (CTxMemPool *)appbase::CBase::Instance().FindComponent<ITxMempoolComponent>();
    // New best block
    //    txmempool->AddTransactionsUpdated(1); todu

    cvBlockChange.notify_all();

    std::vector<std::string> warningMessages;
    if (!IsInitialBlockDownload())
    {
        int nUpgraded = 0;
        const CBlockIndex *pindex = chainActive.Tip();
        for (int bit = 0; bit < VERSIONBITS_NUM_BITS; bit++)
        {
            WarningBitsConditionChecker checker(bit);
            ThresholdState state = checker.GetStateFor(pindex, chainParams.GetConsensus(), warningcache[bit]);
            if (state == THRESHOLD_ACTIVE || state == THRESHOLD_LOCKED_IN)
            {
                const std::string strWarning = strprintf(_("Warning: unknown new rules activated (versionbit %i)"),
                                                         bit);
                if (state == THRESHOLD_ACTIVE)
                {
                    DoWarning(strWarning);
                } else
                {
                    warningMessages.push_back(strWarning);
                }
            }
        }
        // Check the version of the last 100 blocks to see if we need to upgrade:
        for (int i = 0; i < 100 && pindex != nullptr; i++)
        {
            int32_t nExpectedVersion = ComputeBlockVersion(pindex->pprev, chainParams.GetConsensus());
            if (pindex->nVersion > VERSIONBITS_LAST_OLD_BLOCK_VERSION && (pindex->nVersion & ~nExpectedVersion) != 0)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            warningMessages.push_back(strprintf(_("%d of last 100 blocks have unexpected version"), nUpgraded));
        if (nUpgraded > 100 / 2)
        {
            std::string strWarning = _(
                    "Warning: Unknown block versions being mined! It's possible unknown rules are in effect");
            //             notify GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            DoWarning(strWarning);
        }
    }
    LogPrintf(
            "%s: new best=%s height=%d version=0x%08x log2_work=%.8g tx=%lu date='%s' progress=%f cache=%.1fMiB(%utxo)",
            __func__,
            chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(), chainActive.Tip()->nVersion,
            log(chainActive.Tip()->nChainWork.getdouble()) / log(2.0), (unsigned long)chainActive.Tip()->nChainTx,
            DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
            GuessVerificationProgress(chainParams.TxData(), chainActive.Tip()),
            cViewManager.GetCoinsTip()->DynamicMemoryUsage() * (1.0 / (1 << 20)),
            cViewManager.GetCoinsTip()->GetCacheSize());
    if (!warningMessages.empty())
        LogPrintf(" warning='%s'", boost::algorithm::join(warningMessages, ", "));
    LogPrintf("\n");

}

bool CChainCommonent::DisconnectTip(CValidationState &state, const CChainParams &chainparams,
                                    DisconnectedBlockTransactions *disconnectpool)
{
    CBlockIndex *pIndexDelete = Tip();
    assert(pIndexDelete);

    // Read block from disk.
    std::shared_ptr<CBlock> pBlock = std::make_shared<CBlock>();
    CBlock &block = *pBlock;
    const CChainParams &params = appbase::app().GetChainParams();

    if (!ReadBlockFromDisk(block, pIndexDelete, params.GetConsensus()))
    {
        return AbortNode(state, "Failed to read block");
    }
    // Apply the block atomically to the chain state.
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(cViewManager.GetCoinsTip());
        assert(view.GetBestBlock() == pIndexDelete->GetBlockHash());
        if (cViewManager.DisconnectBlock(block, pIndexDelete, view))
        {
            return error("DisconnectTip(): DisconnectBlock %s failed", pIndexDelete->GetBlockHash().ToString());
        }

        bool bFlush = view.Flush();
        assert(bFlush);
    }

    LogPrint(BCLog::BENCH, "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);

    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED, chainparams))
    {
        return false;
    }

    if (disconnectpool)
    {
        // Save transactions to re-add to mempool at end of reorg
        for (auto it = block.vtx.rbegin(); it != block.vtx.rend(); ++it)
        {
            disconnectpool->addTransaction(*it);
        }

        ITxMempoolComponent *txmempool = (CTxMemPool *)appbase::CBase::Instance().FindComponent<ITxMempoolComponent>();

        while (disconnectpool->DynamicMemoryUsage() > MAX_DISCONNECTED_TX_POOL_SIZE * 1000)
        {
            // Drop the earliest entry, and remove its children from the mempool.
            auto it = disconnectpool->queuedTx.get<insertion_order>().begin();
            //            txmempool->removeRecursive(**it, MemPoolRemovalReason::REORG);  todu
            disconnectpool->removeEntry(it);
        }
    }

    // Update chainActive and related variables.
    UpdateTip(pIndexDelete->pprev, chainparams);

    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    GetMainSignals().BlockDisconnected(pBlock);
    return true;
}

static bool
CheckBlockHeader(const CBlockHeader &block, CValidationState &state, const Consensus::Params &consensusParams,
                 bool fCheckPOW = true)
{
    // Check proof of work matches claimed amount
    if (fCheckPOW && !CheckProofOfWork(block.GetHash(), block.nBits, consensusParams))
        return state.DoS(50, false, REJECT_INVALID, "high-hash", false, "proof of work failed");

    return true;
}

bool CChainCommonent::CheckBlock(const CBlock &block, CValidationState &state, const Consensus::Params &consensusParams,
                                 bool fCheckPOW,
                                 bool fCheckMerkleRoot)
{
    // These are checks that are independent of context.

    if (block.fChecked)
        return true;

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, consensusParams, fCheckPOW))
        return false;

    // Check the merkle root.
    if (fCheckMerkleRoot)
    {
        bool mutated;
        uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS(100, false, REJECT_INVALID, "bad-txnmrklroot", true, "hashMerkleRoot mismatch");

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (mutated)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-duplicate", true, "duplicate transaction");
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.
    // Note that witness malleability is checked in ContextualCheckBlock, so no
    // checks that use witness data may be performed here.

    // Size limits
    if (block.vtx.empty() || block.vtx.size() * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT ||
        ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) *
        WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-length", false, "size limits failed");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-missing", false, "first tx is not coinbase");
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i]->IsCoinBase())
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-multiple", false, "more than one coinbase");

    // Check transactions
    ITxMempoolComponent *txmempool = (CTxMemPool *)appbase::CBase::Instance().FindComponent<ITxMempoolComponent>();
    for (const auto &tx : block.vtx)
    {
        // todu
        //        if (!txmempool->CheckTransaction(*tx, state, false))
        //        {
        //            return state.Invalid(false, state.GetRejectCode(), state.GetRejectReason(),
        //                                 strprintf("Transaction check failed (tx hash %s) %s", tx->GetHash().ToString(),
        //                                           state.GetDebugMessage()));
        //        }

    }


    unsigned int nSigOps = 0;
    for (const auto &tx : block.vtx)
    {
        // todu
        //        nSigOps += txmempool->GetLegacySigOpCount(*tx);
    }
    if (nSigOps * WITNESS_SCALE_FACTOR > MAX_BLOCK_SIGOPS_COST)
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-sigops", false, "out-of-bounds SigOpCount");

    if (fCheckPOW && fCheckMerkleRoot)
        block.fChecked = true;

    return true;
}

static unsigned int GetBlockScriptFlags(const CBlockIndex *pindex, const Consensus::Params &consensusparams)
{
    AssertLockHeld(cs_main);

    // BIP16 didn't become active until Apr 1 2012
    int64_t nBIP16SwitchTime = 1333238400;
    bool fStrictPayToScriptHash = (pindex->GetBlockTime() >= nBIP16SwitchTime);

    unsigned int flags = fStrictPayToScriptHash ? SCRIPT_VERIFY_P2SH : SCRIPT_VERIFY_NONE;

    // Start enforcing the DERSIG (BIP66) rule
    if (pindex->nHeight >= consensusparams.BIP66Height)
    {
        flags |= SCRIPT_VERIFY_DERSIG;
    }

    // Start enforcing CHECKLOCKTIMEVERIFY (BIP65) rule
    if (pindex->nHeight >= consensusparams.BIP65Height)
    {
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic.
    if (VersionBitsState(pindex->pprev, consensusparams, Consensus::DEPLOYMENT_CSV, versionbitscache) ==
        THRESHOLD_ACTIVE)
    {
        flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
    }

    // Start enforcing WITNESS rules using versionbits logic.
    if (IsWitnessEnabled(pindex->pprev, consensusparams))
    {
        flags |= SCRIPT_VERIFY_WITNESS;
        flags |= SCRIPT_VERIFY_NULLDUMMY;
    }

    // If the sbtc fork is enabled
    if (IsSBTCForkEnabled(consensusparams, pindex->pprev))
    {
        flags |= SCRIPT_ENABLE_SIGHASH_SBTC_FORK;
    }

    return flags;
}

/** Apply the effects of this block (with given index) on the UTXO set represented by coins.
 *  Validity checks that depend on the UTXO set are also done; ConnectBlock()
 *  can fail if those validity checks fail (among other reasons).
*/
bool
CChainCommonent::ConnectBlock(const CBlock &block, CValidationState &state, CBlockIndex *pindex, CCoinsViewCache &view,
                              const CChainParams &chainparams, bool fJustCheck)
{
    AssertLockHeld(cs);
    assert(pindex);
    // pindex->phashBlock can be null if called by CreateNewBlock/TestBlockValidity
    assert((pindex->phashBlock == nullptr) || (*pindex->phashBlock == block.GetHash()));
    int64_t nTimeStart = GetTimeMicros();

    // Check it again in case a previous version let a bad block in
    if (!CheckBlock(block, state, chainparams.GetConsensus(), !fJustCheck, !fJustCheck))
        return error("%s: Consensus::CheckBlock: %s", __func__, FormatStateMessage(state));

    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = pindex->pprev == nullptr ? uint256() : pindex->pprev->GetBlockHash();
    assert(hashPrevBlock == view.GetBestBlock());

    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if (block.GetHash() == chainparams.GetConsensus().hashGenesisBlock)
    {
        if (!fJustCheck)
            view.SetBestBlock(pindex->GetBlockHash());
        return true;
    }

    bool fScriptChecks = true;
    if (!hashAssumeValid.IsNull())
    {
        // We've been configured with the hash of a block which has been externally verified to have a valid history.
        // A suitable default value is included with the software and updated from time to time.  Because validity
        //  relative to a piece of software is an objective fact these defaults can be easily reviewed.
        // This setting doesn't force the selection of any particular chain but makes validating some faster by
        //  effectively caching the result of part of the verification.
        BlockMap::const_iterator it = mapBlockIndex.find(hashAssumeValid);
        if (it != mapBlockIndex.end())
        {
            if (it->second->GetAncestor(pindex->nHeight) == pindex &&
                pindexBestHeader->GetAncestor(pindex->nHeight) == pindex &&
                pindexBestHeader->nChainWork >= nMinimumChainWork)
            {
                // This block is a member of the assumed verified chain and an ancestor of the best header.
                // The equivalent time check discourages hash power from extorting the network via DOS attack
                //  into accepting an invalid block through telling users they must manually set assumevalid.
                //  Requiring a software change or burying the invalid block, regardless of the setting, makes
                //  it hard to hide the implication of the demand.  This also avoids having release candidates
                //  that are hardly doing any signature verification at all in testing without having to
                //  artificially set the default assumed verified block further back.
                // The test against nMinimumChainWork prevents the skipping when denied access to any chain at
                //  least as good as the expected chain.
                fScriptChecks = (GetBlockProofEquivalentTime(*pindexBestHeader, *pindex, *pindexBestHeader,
                                                             chainparams.GetConsensus()) <= 60 * 60 * 24 * 7 * 2);
            }
        }
    }

    int64_t nTime1 = GetTimeMicros();
    nTimeCheck += nTime1 - nTimeStart;
    LogPrint(BCLog::BENCH, "    - Sanity checks: %.2fms [%.2fs]\n", 0.001 * (nTime1 - nTimeStart),
             nTimeCheck * 0.000001);

    // Do not allow blocks that contain transactions which 'overwrite' older transactions,
    // unless those are already completely spent.
    // If such overwrites are allowed, coinbases and transactions depending upon those
    // can be duplicated to remove the ability to spend the first instance -- even after
    // being sent to another address.
    // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
    // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
    // already refuses previously-known transaction ids entirely.
    // This rule was originally applied to all blocks with a timestamp after March 15, 2012, 0:00 UTC.
    // Now that the whole chain is irreversibly beyond that time it is applied to all blocks except the
    // two in the chain that violate it. This prevents exploiting the issue against nodes during their
    // initial block download.
    bool fEnforceBIP30 = (!pindex->phashBlock) || // Enforce on CreateNewBlock invocations which don't have a hash.
                         !((pindex->nHeight == 91842 && pindex->GetBlockHash() == uint256S(
                                 "0x00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec")) ||
                           (pindex->nHeight == 91880 && pindex->GetBlockHash() == uint256S(
                                   "0x00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721")));

    // Once BIP34 activated it was not possible to create new duplicate coinbases and thus other than starting
    // with the 2 existing duplicate coinbase pairs, not possible to create overwriting txs.  But by the
    // time BIP34 activated, in each of the existing pairs the duplicate coinbase had overwritten the first
    // before the first had been spent.  Since those coinbases are sufficiently buried its no longer possible to create further
    // duplicate transactions descending from the known pairs either.
    // If we're on the known chain at height greater than where BIP34 activated, we can save the db accesses needed for the BIP30 check.
    CBlockIndex *pindexBIP34height = pindex->pprev->GetAncestor(chainparams.GetConsensus().BIP34Height);
    //Only continue to enforce if we're below BIP34 activation height or the block hash at that height doesn't correspond.
    fEnforceBIP30 = fEnforceBIP30 && (!pindexBIP34height ||
                                      !(pindexBIP34height->GetBlockHash() == chainparams.GetConsensus().BIP34Hash));

    if (fEnforceBIP30)
    {
        for (const auto &tx : block.vtx)
        {
            for (size_t o = 0; o < tx->vout.size(); o++)
            {
                if (view.HaveCoin(COutPoint(tx->GetHash(), o)))
                {
                    return state.DoS(100, error("ConnectBlock(): tried to overwrite transaction"),
                                     REJECT_INVALID, "bad-txns-BIP30");
                }
            }
        }
    }

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic.
    int nLockTimeFlags = 0;
    if (VersionBitsState(pindex->pprev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_CSV, versionbitscache) ==
        THRESHOLD_ACTIVE)
    {
        nLockTimeFlags |= LOCKTIME_VERIFY_SEQUENCE;
    }

    // Get the script flags for this block
    unsigned int flags = GetBlockScriptFlags(pindex, chainparams.GetConsensus());

    int64_t nTime2 = GetTimeMicros();
    nTimeForks += nTime2 - nTime1;
    LogPrint(BCLog::BENCH, "    - Fork checks: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeForks * 0.000001);

    CBlockUndo blockundo;

    CCheckQueueControl<CScriptCheck> control(fScriptChecks && nScriptCheckThreads ? &scriptcheckqueue : nullptr);

    std::vector<int> prevheights;
    CAmount nFees = 0;
    int nInputs = 0;
    int64_t nSigOpsCost = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    std::vector<PrecomputedTransactionData> txdata;
    // Required so that pointers to individual PrecomputedTransactionData don't get invalidated
    txdata.reserve(block.vtx.size());
    ITxMempoolComponent *txmempool = (CTxMemPool *)appbase::CBase::Instance().FindComponent<ITxMempoolComponent>();
    for (unsigned int i = 0; i < block.vtx.size(); i++)
    {
        const CTransaction &tx = *(block.vtx[i]);

        // todu
        //        nInputs += tx.vin.size();
        //
        //        if (!tx.IsCoinBase())
        //        {
        //            if (!view.HaveInputs(tx))
        //                return state.DoS(100, error("ConnectBlock(): inputs missing/spent"),
        //                                 REJECT_INVALID, "bad-txns-inputs-missingorspent");
        //
        //            // Check that transaction is BIP68 final
        //            // BIP68 lock checks (as opposed to nLockTime checks) must
        //            // be in ConnectBlock because they require the UTXO set
        //            prevheights.resize(tx.vin.size());
        //            for (size_t j = 0; j < tx.vin.size(); j++)
        //            {
        //                prevheights[j] = view.AccessCoin(tx.vin[j].prevout).nHeight;
        //            }
        //
        //            if (!txmempool->SequenceLocks(tx, nLockTimeFlags, &prevheights, *pindex))
        //            {
        //                return state.DoS(100, error("%s: contains a non-BIP68-final transaction", __func__),
        //                                 REJECT_INVALID, "bad-txns-nonfinal");
        //            }
        //        }
        //
        //        // GetTransactionSigOpCost counts 3 types of sigops:
        //        // * legacy (always)
        //        // * p2sh (when P2SH enabled in flags and excludes coinbase)
        //        // * witness (when witness enabled in flags and excludes coinbase)
        //        nSigOpsCost += txmempool->GetTransactionSigOpCost(tx, view, flags);
        //        if (nSigOpsCost > MAX_BLOCK_SIGOPS_COST)
        //            return state.DoS(100, error("ConnectBlock(): too many sigops"),
        //                             REJECT_INVALID, "bad-blk-sigops");
        //
        //        txdata.emplace_back(tx);
        //        if (!tx.IsCoinBase())
        //        {
        //            nFees += view.GetValueIn(tx) - tx.GetValueOut();
        //
        //            std::vector<CScriptCheck> vChecks;
        //            bool fCacheResults = fJustCheck; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
        //            if (!CheckInputs(tx, state, view, fScriptChecks, flags, fCacheResults, fCacheResults, txdata[i],
        //                             nScriptCheckThreads ? &vChecks : nullptr))
        //                return error("ConnectBlock(): CheckInputs on %s failed with %s",
        //                             tx.GetHash().ToString(), FormatStateMessage(state));
        //            control.Add(vChecks);
        //        }
        //
        //        CTxUndo undoDummy;
        //        if (i > 0)
        //        {
        //            blockundo.vtxundo.push_back(CTxUndo());
        //        }
        //        cViewManager.UpdateCoins(tx, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);
        //
        //        vPos.push_back(std::make_pair(tx.GetHash(), pos));
        //        pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
    }
    int64_t nTime3 = GetTimeMicros();
    nTimeConnect += nTime3 - nTime2;
    LogPrint(BCLog::BENCH, "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n",
             (unsigned)block.vtx.size(), 0.001 * (nTime3 - nTime2), 0.001 * (nTime3 - nTime2) / block.vtx.size(),
             nInputs <= 1 ? 0 : 0.001 * (nTime3 - nTime2) / (nInputs - 1), nTimeConnect * 0.000001);

    CAmount blockReward = 0;
    blockReward = nFees + GetBlockSubsidy(pindex->nHeight, chainparams.GetConsensus());
    if (block.vtx[0]->GetValueOut() > blockReward)
        return state.DoS(100,
                         error("ConnectBlock(): coinbase pays too much (actual=%d vs limit=%d)",
                               block.vtx[0]->GetValueOut(), blockReward),
                         REJECT_INVALID, "bad-cb-amount");

    if (!control.Wait())
        return state.DoS(100, error("%s: CheckQueue failed", __func__), REJECT_INVALID, "block-validation-failed");
    int64_t nTime4 = GetTimeMicros();
    nTimeVerify += nTime4 - nTime2;
    LogPrint(BCLog::BENCH, "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1,
             0.001 * (nTime4 - nTime2), nInputs <= 1 ? 0 : 0.001 * (nTime4 - nTime2) / (nInputs - 1),
             nTimeVerify * 0.000001);

    if (fJustCheck)
        return true;

    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS))
    {
        // todo
        //        if (pindex->GetUndoPos().IsNull())
        //        {
        //            CDiskBlockPos _pos;
        //            if (!FindUndoPos(state, pindex->nFile, _pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
        //                return error("ConnectBlock(): FindUndoPos failed");
        //            if (!UndoWriteToDisk(blockundo, _pos, pindex->pprev->GetBlockHash(), chainparams.MessageStart()))
        //                return AbortNode(state, "Failed to write undo data");
        //
        //            // update nUndoPos in block index
        //            pindex->nUndoPos = _pos.nPos;
        //            pindex->nStatus |= BLOCK_HAVE_UNDO;
        //        }
        //
        //        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        //        setDirtyBlockIndex.insert(pindex);
    }

    if (fTxIndex)
        if (!pblocktree->WriteTxIndex(vPos))
            return AbortNode(state, "Failed to write transaction index");

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    int64_t nTime5 = GetTimeMicros();
    nTimeIndex += nTime5 - nTime4;
    LogPrint(BCLog::BENCH, "    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime5 - nTime4), nTimeIndex * 0.000001);

    int64_t nTime6 = GetTimeMicros();
    nTimeCallbacks += nTime6 - nTime5;
    LogPrint(BCLog::BENCH, "    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime6 - nTime5), nTimeCallbacks * 0.000001);
    return true;
}

/**
 * Connect a new block to chainActive. pblock is either nullptr or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 *
 * The block is added to connectTrace if connection succeeds.
 */
bool CChainCommonent::ConnectTip(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pIndexNew,
                                 const std::shared_ptr<const CBlock> &pblock, ConnectTrace &connectTrace,
                                 DisconnectedBlockTransactions &disconnectpool)
{
    assert(pIndexNew->pprev == Tip());
    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    std::shared_ptr<const CBlock> pthisBlock;
    if (!pblock)
    {
        std::shared_ptr<CBlock> pblockNew = std::make_shared<CBlock>();
        if (!ReadBlockFromDisk(*pblockNew, pIndexNew, chainparams.GetConsensus()))
            return AbortNode(state, "Failed to read block");
        pthisBlock = pblockNew;
    } else
    {
        pthisBlock = pblock;
    }
    const CBlock &blockConnecting = *pthisBlock;
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros();
    nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    LogPrint(BCLog::BENCH, "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001,
             nTimeReadFromDisk * 0.000001);
    {
        CCoinsViewCache view(cViewManager.GetCoinsTip());
        bool rv = ConnectBlock(blockConnecting, state, pIndexNew, view, chainparams);
        GetMainSignals().BlockChecked(blockConnecting, state);
        if (!rv)
        {
            if (state.IsInvalid())
                cIndexManager.InvalidBlockFound(pIndexNew, state);
            return error("ConnectTip(): ConnectBlock %s failed", pIndexNew->GetBlockHash().ToString());
        }
        nTime3 = GetTimeMicros();
        nTimeConnectTotal += nTime3 - nTime2;
        LogPrint(BCLog::BENCH, "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001,
                 nTimeConnectTotal * 0.000001);
        bool flushed = view.Flush();
        assert(flushed);
    }
    int64_t nTime4 = GetTimeMicros();
    nTimeFlush += nTime4 - nTime3;
    LogPrint(BCLog::BENCH, "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED, chainparams))
        return false;
    int64_t nTime5 = GetTimeMicros();
    nTimeChainState += nTime5 - nTime4;
    LogPrint(BCLog::BENCH, "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001,
             nTimeChainState * 0.000001);

    // Remove conflicting transactions from the mempool.;
    ITxMempoolComponent *txmempool = (CTxMemPool *)appbase::CBase::Instance().FindComponent<ITxMempoolComponent>();
    //    txmempool->removeForBlock(blockConnecting.vtx, pIndexNew->nHeight); todu
    disconnectpool.removeForBlock(blockConnecting.vtx);
    // Update chainActive & related variables.
    UpdateTip(pIndexNew, chainparams);

    int64_t nTime6 = GetTimeMicros();
    nTimePostConnect += nTime6 - nTime5;
    nTimeTotal += nTime6 - nTime1;
    LogPrint(BCLog::BENCH, "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001,
             nTimePostConnect * 0.000001);
    LogPrint(BCLog::BENCH, "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);

    connectTrace.BlockConnected(pIndexNew, std::move(pthisBlock));
    return true;
}

void CChainCommonent::CheckForkWarningConditions()
{
    AssertLockHeld(cs);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before finishing our initial sync)
    if (IsInitialBlockDownload())
        return;

    cIndexManager.CheckForkWarningConditions();
}

bool CChainCommonent::ActivateBestChainStep(CValidationState &state, const CChainParams &chainparams,
                                            CBlockIndex *pIndexMostWork,
                                            const std::shared_ptr<const CBlock> &pblock, bool &bInvalidFound,
                                            ConnectTrace &connectTrace)
{
    AssertLockHeld(cs);
    const CBlockIndex *pIndexOldTip = Tip();
    const CBlockIndex *pIndexFork = cIndexManager.GetChain().FindFork(pIndexMostWork);

    ITxMempoolComponent *txmempool = (CTxMemPool *)appbase::CBase::Instance().FindComponent<ITxMempoolComponent>();

    // Disconnect active blocks which are no longer in the best chain.
    bool bBlocksDisconnected = false;
    DisconnectedBlockTransactions disconnectPool;
    while (Tip() && Tip() != pIndexFork)
    {
        if (!DisconnectTip(state, chainparams, &disconnectPool))
        {
            // This is likely a fatal error, but keep the mempool consistent,
            // just in case. Only remove from the mempool in this case.
            txmempool->UpdateMempoolForReorg(disconnectPool, false);
            return false;
        }
        bBlocksDisconnected = true;
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex *> vecIndexToConnect;
    bool bContinue = true;
    int iHeight = pIndexFork ? pIndexFork->nHeight : -1;
    while (bContinue && iHeight != pIndexMostWork->nHeight)
    {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int iTargetHeight = std::min(iHeight + 32, pIndexMostWork->nHeight);
        vecIndexToConnect.clear();
        vecIndexToConnect.reserve(iTargetHeight - iHeight);
        CBlockIndex *pIndexIter = pIndexMostWork->GetAncestor(iTargetHeight);
        while (pIndexIter && pIndexIter->nHeight != iHeight)
        {
            vecIndexToConnect.push_back(pIndexIter);
            pIndexIter = pIndexIter->pprev;
        }
        iHeight = iTargetHeight;

        // Connect new blocks.
        for (CBlockIndex *pIndexConnect : reverse_iterate(vecIndexToConnect))
        {
            if (!ConnectTip(state, chainparams, pIndexConnect,
                            pIndexConnect == pIndexMostWork ? pblock : std::shared_ptr<const CBlock>(), connectTrace,
                            disconnectPool))
            {
                if (state.IsInvalid())
                {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                    {
                        cIndexManager.InvalidChainFound(vecIndexToConnect.back());
                    }
                    state = CValidationState();
                    bInvalidFound = true;
                    bContinue = false;
                    break;
                } else
                {
                    // A system error occurred (disk space, database error, ...).
                    // Make the mempool consistent with the current tip, just in case
                    // any observers try to use it before shutdown.
                    //                    UpdateMempoolForReorg(disconnectpool, false);
                    txmempool->UpdateMempoolForReorg(disconnectPool, false);
                    return false;
                }
            } else
            {
                cIndexManager.PruneBlockIndexCandidates();
                if (!pIndexOldTip || Tip()->nChainWork > pIndexOldTip->nChainWork)
                {
                    // We're in a better position than we were. Return temporarily to release the lock.
                    bContinue = false;
                    break;
                }
            }
        }
    }

    if (bBlocksDisconnected)
    {
        // If any blocks were disconnected, disconnectpool may be non empty.  Add
        // any disconnected transactions back to the mempool.
        txmempool->UpdateMempoolForReorg(disconnectPool, true);
    }

    mempool.check(cViewManager.GetCoinsTip());

    // Callbacks/notifications for a new best chain.
    if (bInvalidFound)
    {
        cIndexManager.CheckForkWarningConditionsOnNewFork(vecIndexToConnect.back());
    } else
    {
        CheckForkWarningConditions();
    }

    return true;
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either nullptr or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool CChainCommonent::ActivateBestChain(CValidationState &state, const CChainParams &chainparams,
                                        std::shared_ptr<const CBlock> pblock)
{
    // Note that while we're often called here from ProcessNewBlock, this is
    // far from a guarantee. Things in the P2P/RPC will often end up calling
    // us in the middle of ProcessNewBlock - do not assume pblock is set
    // sanely for performance or correctness!

    CBlockIndex *pIndexMostWork = nullptr;
    CBlockIndex *pIndexNewTip = nullptr;

    const CChainParams &params = appbase::app().GetChainParams();
    const CArgsManager &appArgs = appbase::app().GetArgsManager();

    int iStopAtHeight = appArgs.GetArg<int>("-stopatheight", DEFAULT_STOPATHEIGHT);
    do
    {
        boost::this_thread::interruption_point();
        if (ShutdownRequested())
            break;

        const CBlockIndex *pIndexFork;
        bool bInitialDownload;
        {
            LOCK(cs);
            ConnectTrace connectTrace(mempool);

            CBlockIndex *pIndexOldTip = Tip();
            if (pIndexMostWork == nullptr)
            {
                pIndexMostWork = cIndexManager.FindMostWorkIndex();
            }

            // Whether we have anything to do at all.
            if (pIndexMostWork == nullptr || pIndexMostWork == pIndexOldTip)
            {
                return true;
            }

            bool bInvalidFound = false;
            std::shared_ptr<const CBlock> nullBlockPtr;
            if (!ActivateBestChainStep(state, chainparams, pIndexMostWork,
                                       pblock && pblock->GetHash() == pIndexMostWork->GetBlockHash() ? pblock
                                                                                                     : nullBlockPtr,
                                       bInvalidFound, connectTrace))
            {
                return false;
            }

            if (bInvalidFound)
            {
                // Wipe cache, we may need another branch now.
                pIndexMostWork = nullptr;
            }
            pIndexNewTip = Tip();
            pIndexFork = cIndexManager.GetChain().FindFork(pIndexOldTip);
            bInitialDownload = IsInitialBlockDownload();

            // block connect event
            for (const PerBlockConnectTrace &trace : connectTrace.GetBlocksConnected())
            {
                assert(trace.pblock && trace.pindex);
                GetMainSignals().BlockConnected(trace.pblock, trace.pindex, *trace.conflictedTxs);
            }
        }
        // When we reach this point, we switched to a new tip (stored in pindexNewTip).

        // Notifications/callbacks that can run without cs_main

        // Notify external listeners about the new tip.
        GetMainSignals().UpdatedBlockTip(pIndexNewTip, pIndexFork, bInitialDownload);

        // Always notify the UI if a new block tip was connected
        if (pIndexFork != pIndexNewTip)
        {
            uiInterface.NotifyBlockTip(bInitialDownload, pIndexNewTip);
        }

        if (iStopAtHeight && pIndexNewTip && pIndexNewTip->nHeight >= iStopAtHeight)
            StartShutdown();
    } while (pIndexNewTip != pIndexMostWork);

    cIndexManager.CheckBlockIndex(params.GetConsensus());
    return true;
}

bool CChainCommonent::InvalidateBlock(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pindex)
{
    AssertLockHeld(cs);

    // We first disconnect backwards and then mark the blocks as invalid.
    // This prevents a case where pruned nodes may fail to invalidateblock
    // and be left unable to start as they have no tip candidates (as there
    // are no blocks that meet the "have data and are not invalid per
    // nStatus" criteria for inclusion in setBlockIndexCandidates).

    bool pindex_was_in_chain = false;
    CChain chainActive = cIndexManager.GetChain();

    CBlockIndex *invalid_walk_tip = chainActive.Tip();

    CTxMemPool *txmempool = (CTxMemPool *)appbase::CBase::Instance().FindComponent<CTxMemPool>();

    DisconnectedBlockTransactions disconnectpool;
    while (chainActive.Contains(pindex))
    {
        pindex_was_in_chain = true;
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state, chainparams, &disconnectpool))
        {
            // It's probably hopeless to try to make the mempool consistent
            // here if DisconnectTip failed, but we can try.
            txmempool->UpdateMempoolForReorg(disconnectpool, false);
            return false;
        }
    }

    // Now mark the blocks we just disconnected as descendants invalid
    // (note this may not be all descendants).
    cIndexManager.InvalidateBlock(pindex, invalid_walk_tip, pindex_was_in_chain);

    // DisconnectTip will add transactions to disconnectpool; try to add these
    // back to the mempool.
    txmempool->UpdateMempoolForReorg(disconnectpool, true);

    cIndexManager.InvalidChainFound(pindex);
    uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindex->pprev);
    return true;
}

bool CChainCommonent::CheckActiveChain(CValidationState &state, const CChainParams &chainparams)
{
    LOCK(cs);

    CBlockIndex *pOldTipIndex = Tip();  // 1. current block chain tip
    LogPrint(BCLog::BENCH, "Current tip block:%s\n", pOldTipIndex->ToString().c_str());
    MapCheckpoints checkpoints = chainparams.Checkpoints().mapCheckpoints;

    if (checkpoints.rbegin()->first < 1)
        return true;

    CChain chainActive = cIndexManager.GetChain();
    if (chainActive.Height() >= checkpoints.rbegin()->first &&
        chainActive[checkpoints.rbegin()->first]->GetBlockHash() == checkpoints.rbegin()->second)
    {
        return true;
    }


    auto GetFirstCheckPointInChain = [&]()
    {
        auto itReturn = checkpoints.rbegin();
        for (; itReturn != checkpoints.rend(); itReturn++)
        {
            if (chainActive[itReturn->first] != nullptr &&
                chainActive[itReturn->first]->GetBlockHash() == itReturn->second)
            {
                break;
            }
        }
        return itReturn;
    };

    auto GetFirstCheckPointNotInChain = [&](MapCheckpoints::const_reverse_iterator firstInChain)
    {
        assert(firstInChain != checkpoints.rbegin());
        firstInChain--;
        assert(chainActive[firstInChain->first] == nullptr ||
               chainActive[firstInChain->first]->GetBlockHash() != firstInChain->second);
        return firstInChain;
    };

    auto firstInChainPoint = GetFirstCheckPointInChain();
    assert(checkpoints.rbegin() != firstInChainPoint);
    auto firstNotInChainPoint = GetFirstCheckPointNotInChain(firstInChainPoint);


    if (firstNotInChainPoint->first > chainActive.Height())
    {
        return true;
    }

    CBlockIndex *desPoint = chainActive[firstNotInChainPoint->first];


    if (!InvalidateBlock(state, chainparams, desPoint))
    {
        return false;
    }

    if (state.IsValid())
    {
        ActivateBestChain(state, chainparams, nullptr);
    }

    if (!state.IsValid())
    {

        LogPrint(BCLog::BENCH, "reject reason %s\n", state.GetRejectReason());
        return false;

    }


    if (chainActive.Tip() != pOldTipIndex)
    {
        // Write changes  to disk.
        if (!FlushStateToDisk(state, FLUSH_STATE_ALWAYS, chainparams))
        {
            return false;
        }
        uiInterface.NotifyBlockTip(IsInitialBlockDownload(), chainActive.Tip());
    }

    LogPrint(BCLog::BENCH, "CheckActiveChain End====\n");

    return true;
}