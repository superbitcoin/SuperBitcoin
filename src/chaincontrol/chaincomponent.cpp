#include <iostream>
#include "chaincomponent.h"
#include "sbtccore/streams.h"
#include "interface/inetcomponent.h"
#include "utils/net/netmessagehelper.h"
#include "sbtccore/block/validation.h"
#include "utils/reverse_iterator.h"

CChainCommonent::CChainCommonent()
{
}

CChainCommonent::~CChainCommonent()
{
}

bool CChainCommonent::ComponentInitialize()
{
    std::cout << "initialize chain component \n";
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


bool CChainCommonent::NetRequestCheckPoint(ExNode *xnode, int height)
{
    assert(xnode != nullptr);

    std::set<int> &checkpointKnown = m_nodeCheckPointKnown[xnode->nodeID];

    std::vector<Checkpoints::CCheckData> vSendData;
    std::vector<Checkpoints::CCheckData> vnHeight;
    Checkpoints::GetCheckpointByHeight(height, vnHeight);
    for (const auto &point : vnHeight)
    {
        if (checkpointKnown.count(point.getHeight()) == 0)
        {
            checkpointKnown.insert(point.getHeight());
            vSendData.push_back(point);
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
                m_nodeCheckPointKnown[xnode->nodeID].insert(point.getHeight());
                vIndex.push_back(point);
            }
        } else
        {
            LogPrint(BCLog::NET, "check point signature check failed \n");
            break;
        }
        LogPrint(BCLog::BENCH, "block height=%d, block hash=%s\n", point.getHeight(), point.getHash().ToString());
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

bool CChainCommonent::NetRequestHeaders(ExNode* xnode, CDataStream& stream)
{
    assert(xnode != nullptr);

    if (IsInitialBlockDownload() && !IsFlagsBitOn(xnode->flags, NF_WHITELIST))
    {
        LogPrint(BCLog::NET, "Ignoring getheaders from peer=%d because node is in initial block download\n", xnode->nodeID);
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
    }
    else
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

bool CChainCommonent::NetReceiveHeaders(ExNode* xnode, CDataStream& stream)
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

bool CChainCommonent::NetReceiveHeaders(ExNode* xnode, const std::vector<CBlockHeader>& headers)
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

        bool fCanDirectFetch = Tip()->GetBlockTime() > GetAdjustedTime()
             - appbase::app().GetChainParams().GetConsensus().nPowTargetSpacing * 20;

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

bool CChainCommonent::NetReceiveBlock(ExNode* xnode, CDataStream& stream, uint256& blockHash)
{
    assert(xnode != nullptr);

    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    stream >> *pblock;

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
    ProcessNewBlock(Params(), pblock, forceProcessing, &fNewBlock);
    if (fNewBlock)
    {
        SetFlagsBit(xnode->retFlags, NF_NEWBLOCK);
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

bool CChainCommonent::FlushStateToDisk(CValidationState &state, FlushStateMode mode)
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

bool CChainCommonent::DisconnectTip(CValidationState &state)
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
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
    {
        return false;
    }

    // Update chainActive and related variables.
    SetTip(pIndexDelete->pprev);
    return true;
}

/**
 * Connect a new block to chainActive. pblock is either nullptr or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 *
 * The block is added to connectTrace if connection succeeds.
 */
bool CChainCommonent::ConnectTip(CValidationState &state, CBlockIndex *pIndexNew,
                                 const std::shared_ptr<const CBlock> &pblock)
{
    assert(pIndexNew->pprev == Tip());

    const CChainParams &params = appbase::app().GetChainParams();
    std::shared_ptr<const CBlock> pthisBlock;
    if (!pblock)
    {
        std::shared_ptr<CBlock> pblockNew = std::make_shared<CBlock>();
        if (!ReadBlockFromDisk(*pblockNew, pIndexNew, params.GetConsensus()))
            return AbortNode(state, "Failed to read block");
        pthisBlock = pblockNew;
    } else
    {
        pthisBlock = pblock;
    }

    const CBlock &blockConnecting = *pthisBlock;

    return true;
}

bool CChainCommonent::ActivateBestChainStep(CValidationState &state, CBlockIndex *pIndexMostWork,
                                            const std::shared_ptr<const CBlock> &pblock, bool &fInvalidFound)
{
    const CBlockIndex *pIndexOldTip = Tip();
    const CBlockIndex *pIndexFork = cIndexManager.GetChain().FindFork(pIndexMostWork);

    // Disconnect active blocks which are no longer in the best chain.
    bool bBlocksDisconnected = false;
    while (Tip() && Tip() != pIndexFork)
    {
        if (!DisconnectTip(state))
        {
            // some thing todo
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
            if (!ConnectTip(state, pIndexConnect,
                            pIndexConnect == pIndexMostWork ? pblock : std::shared_ptr<const CBlock>()))
            {
                if (state.IsInvalid())
                {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                    {
                        cIndexManager.InvalidChainFound(vecIndexToConnect.back());
                    }
                    state = CValidationState();
                    fInvalidFound = true;
                    bContinue = false;
                    break;
                } else
                {
                    // A system error occurred (disk space, database error, ...).
                    // Make the mempool consistent with the current tip, just in case
                    // any observers try to use it before shutdown.
                    //                    UpdateMempoolForReorg(disconnectpool, false);
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
    return true;
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either nullptr or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool CChainCommonent::ActivateBestChain(CValidationState &state, std::shared_ptr<const CBlock> pblock)
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

        const CBlockIndex *pIndexFork;
        bool bInitialDownload;
        {
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
            if (!ActivateBestChainStep(state, pIndexMostWork,
                                       pblock && pblock->GetHash() == pIndexMostWork->GetBlockHash() ? pblock
                                                                                                     : nullBlockPtr,
                                       bInvalidFound))
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

            // block connect event todo

        }
    } while (pIndexNewTip != pIndexMostWork);

    cIndexManager.CheckBlockIndex(params.GetConsensus());
    return true;
}