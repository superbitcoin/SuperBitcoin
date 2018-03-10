#include <iostream>
#include <thread>
#include "chaincomponent.h"
#include "checkpoints.h"
#include "sbtccore/streams.h"
#include "interface/inetcomponent.h"
#include "utils/net/netmessagehelper.h"
#include "sbtccore/block/validation.h"
#include "sbtccore/transaction/script/sigcache.h"
#include "utils/reverse_iterator.h"
#include "interface/imempoolcomponent.h"
#include "framework/warnings.h"
#include "utils/util.h"
#include "utils/timedata.h"
#include "sbtccore/block/merkle.h"
#include "sbtccore/checkqueue.h"
#include "sbtccore/block/undo.h"
#include "utils/merkleblock.h"
#include "sbtcd/baseimpl.hpp"
#include "framework/validationinterface.h"
#include "eventmanager/eventmanager.h"
#include "utils.h"

// All of the following cache a recent block, and are protected by cs_most_recent_block
static CCriticalSection cs_most_recent_block;
static std::shared_ptr<const CBlock> most_recent_block;
static std::shared_ptr<const CBlockHeaderAndShortTxIDs> most_recent_compact_block;
static uint256 most_recent_block_hash;
static bool fWitnessesPresentInMostRecentCompactBlock;

void CChainComponent::NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock> &pblock)
{
    GetMainSignals().NewPoWValidBlock(pindex, pblock);

    std::shared_ptr<const CBlockHeaderAndShortTxIDs> pcmpctblock = std::make_shared<const CBlockHeaderAndShortTxIDs>(
            *pblock, true);

    static int nHighestFastAnnounce = 0;
    if (pindex->nHeight <= nHighestFastAnnounce)
        return;

    nHighestFastAnnounce = pindex->nHeight;
    bool fWitnessEnabled = IsWitnessEnabled(pindex->pprev, Params().GetConsensus());
    uint256 hashBlock(pblock->GetHash());
    {
        LOCK(cs_most_recent_block);
        most_recent_block_hash = hashBlock;
        most_recent_block = pblock;
        most_recent_compact_block = pcmpctblock;
        fWitnessesPresentInMostRecentCompactBlock = fWitnessEnabled;
    }

    GET_NET_INTERFACE(ifNetObj);
    ifNetObj->RelayCmpctBlock(pindex, (void *)pcmpctblock.get(), fWitnessEnabled);
}

void CChainComponent::OnNodeDisconnected(int64_t nodeID, bool /*bInBound*/, int /*disconnectReason*/)
{
    LOCK(cs_xnodeGuard);
    m_nodeCheckPointKnown.erase(nodeID);
}

bool CChainComponent::NetRequestCheckPoint(ExNode *xnode, int height)
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

bool CChainComponent::NetReceiveCheckPoint(ExNode *xnode, CDataStream &stream)
{
    assert(xnode != nullptr);

    mlog_error("enter checkpoint");
    mlog_error("receive check block list====");

    std::vector<Checkpoints::CCheckData> vdata;
    stream >> vdata;

    Checkpoints::CCheckPointDB cCheckPointDB;
    std::vector<int> toInsertCheckpoints;
    std::vector<Checkpoints::CCheckData> vIndex;
    for (const auto &point : vdata)
    {
        if (point.CheckSignature(Params().GetCheckPointPKey()))
        {
            if (!cCheckPointDB.ExistCheckpoint(point.getHeight()))
            {
                cCheckPointDB.WriteCheckpoint(point.getHeight(), point);
                /*
                 * add the check point to chainparams
                 */
                Params().AddCheckPoint(point.getHeight(), point.getHash());
                toInsertCheckpoints.push_back(point.getHeight());
                vIndex.push_back(point);
            }
        } else
        {
            mlog_error("check point signature check failed \n");
            break;
        }
        mlog_error("block height=%d, block hash=%s\n", point.getHeight(), point.getHash().ToString());
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
        if (!CheckActiveChain(state, Params()))
        {
            mlog_error("CheckActiveChain error when receive  checkpoint");
            return false;
        }
    }

    //broadcast the check point if it is necessary
    if (vIndex.size() == 1 && vIndex.size() == vdata.size())
    {
        SendNetMessage(-1, NetMsgType::CHECKPOINT, xnode->sendVersion, 0, vdata);
    }

    return true;
}

bool CChainComponent::NetRequestBlocks(ExNode *xnode, CDataStream &stream, std::vector<uint256> &blockHashes)
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
    {
        std::shared_ptr<const CBlock> a_recent_block;
        {
            LOCK(cs_most_recent_block);
            a_recent_block = most_recent_block;
        }
        CValidationState dummy;
        ActivateBestChain(dummy, Params(), a_recent_block);
    }

    LOCK(cs_main);

    // Find the last block the caller has in the main chain
    const CBlockIndex *pindex = cIndexManager.FindForkInGlobalIndex(cIndexManager.GetChain(), locator);

    // Send the rest of the chain
    if (pindex)
        pindex = cIndexManager.GetChain().Next(pindex);

    int nLimit = 500;
    mlog_error("getblocks %d to %s limit %d from peer=%d", (pindex ? pindex->nHeight : -1),
               hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, xnode->nodeID);

    for (; pindex; pindex = cIndexManager.GetChain().Next(pindex))
    {
        if (pindex->GetBlockHash() == hashStop)
        {
            mlog_error("  getblocks stopping at %d %s", pindex->nHeight, pindex->GetBlockHash().ToString());
            break;
        }
        // If pruning, don't inv blocks unless we have on disk and are likely to still have
        // for some reasonable time window (1 hour) that block relay might require.
        const int nPrunedBlocksLikelyToHave = MIN_BLOCKS_TO_KEEP - 3600 / Params().GetConsensus().nPowTargetSpacing;
        if (fPruneMode && (!(pindex->nStatus & BLOCK_HAVE_DATA) ||
                           pindex->nHeight <= Tip()->nHeight - nPrunedBlocksLikelyToHave))
        {
            mlog_error(" getblocks stopping, pruned or too old block at %d %s", pindex->nHeight,
                       pindex->GetBlockHash().ToString());
            break;
        }
        blockHashes.emplace_back(pindex->GetBlockHash());
        if (--nLimit <= 0)
        {
            // When this block is requested, we'll send an inv that'll
            // trigger the peer to getblocks the next batch of inventory.
            mlog_error("  getblocks stopping at limit %d %s", pindex->nHeight,
                       pindex->GetBlockHash().ToString());
            break;
        }
    }
    return true;
}

bool CChainComponent::NetRequestHeaders(ExNode *xnode, CDataStream &stream)
{
    assert(xnode != nullptr);

    if (IsInitialBlockDownload() && !IsFlagsBitOn(xnode->flags, NF_WHITELIST))
    {
        mlog_error("Ignoring getheaders from peer=%d because node is in initial block download",
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
        pindex = GetBlockIndex(hashStop);
        if (!pindex)
        {
            return true;
        }
    } else
    {
        // Find the last block the caller has in the main chain
        pindex = cIndexManager.FindForkInGlobalIndex(cIndexManager.GetChain(), locator);
        if (pindex)
            pindex = cIndexManager.GetChain().Next(pindex);
    }

    // we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at the end
    std::vector<CBlock> vHeaders;
    int nLimit = MAX_HEADERS_RESULTS;

    mlog_error("getheaders %d to %s from peer=%d\n", (pindex ? pindex->nHeight : -1),
               hashStop.IsNull() ? "end" : hashStop.ToString(), xnode->nodeID);

    for (; pindex; pindex = cIndexManager.GetChain().Next(pindex))
    {
        vHeaders.push_back(pindex->GetBlockHeader());
        if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
            break;
    }

    // pindex can be nullptr either if we sent chainActive.Tip() OR
    // if our peer has chainActive.Tip() (and thus we are sending an empty
    // headers message). In both cases it's safe to update
    // pindexBestHeaderSent to be our tip.
    //
    // It is important that we simply reset the BestHeaderSent value here,
    // and not max(BestHeaderSent, newHeaderSent). We might have announced
    // the currently-being-connected tip using a compact block, which
    // resulted in the peer sending a headers request, which we respond to
    // without the new block. By resetting the BestHeaderSent, we ensure we
    // will re-announce the new block via headers (or compact blocks again)
    // in the SendMessages logic.
    xnode->retPointer = (void *)(pindex ? pindex : Tip());
    return SendNetMessage(xnode->nodeID, NetMsgType::HEADERS, xnode->sendVersion, 0, vHeaders);
}

bool CChainComponent::NetReceiveHeaders(ExNode *xnode, CDataStream &stream)
{
    assert(xnode != nullptr);

    std::vector<CBlockHeader> headers;

    // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
    unsigned int nCount = ReadCompactSize(stream);
    if (nCount > MAX_HEADERS_RESULTS)
    {
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

bool CChainComponent::NetReceiveHeaders(ExNode *xnode, const std::vector<CBlockHeader> &headers)
{
    assert(xnode != nullptr);

    size_t nCount = headers.size();
    if (nCount == 0)
        return true;

    GET_NET_INTERFACE(ifNetObj);

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
        xnode->retInteger++;
        SendNetMessage(xnode->nodeID, NetMsgType::GETHEADERS, xnode->sendVersion, 0,
                       cIndexManager.GetChain().GetLocator(GetIndexBestHeader()), uint256());

        mlog_error(
                "received header %s: missing prev block %s, sending getheaders (%d) to end (peer=%d, nUnconnectingHeaders=%d)",
                headers[0].GetHash().ToString(),
                headers[0].hashPrevBlock.ToString(),
                GetIndexBestHeader()->nHeight,
                xnode->nodeID, xnode->retInteger);

        LOCK(cs_main);

        // Set hashLastUnknownBlock for this peer, so that if we
        // eventually get the headers - even from a different peer -
        // we can use this peer to download.
        ifNetObj->UpdateBlockAvailability(xnode->nodeID, headers.back().GetHash());

        if (xnode->retInteger % MAX_UNCONNECTING_HEADERS == 0)
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
    if (!ProcessNewBlockHeaders(headers, state, Params(), &pindexLast, &first_invalid_header))
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
                // update the DoS logic (or, rather, rewrite the
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
        if (xnode->retInteger > 0)
        {
            mlog_notice("peer=%d: resetting nUnconnectingHeaders (%d -> 0)\n", xnode->nodeID,
                        xnode->retInteger);
        }
        xnode->retInteger = 0;

        LOCK(cs_main);

        assert(pindexLast);
        ifNetObj->UpdateBlockAvailability(xnode->nodeID, pindexLast->GetBlockHash());

        // From here, pindexBestKnownBlock should be guaranteed to be non-null,
        // because it is set in UpdateBlockAvailability. Some nullptr checks
        // are still present, however, as belt-and-suspenders.

        if (received_new_header && pindexLast->nChainWork > Tip()->nChainWork)
        {
            SetFlagsBit(xnode->retFlags, NF_LASTBLOCKANNOUNCE);
        }

        if (nCount == MAX_HEADERS_RESULTS)
        {
            // Headers message had its maximum size; the peer may have more headers.
            // optimize: if pindexLast is an ancestor of chainActive.Tip or pindexBestHeader, continue
            // from there instead.
            mlog_error("more getheaders (%d) to end to peer=%d (startheight:%d)\n", pindexLast->nHeight,
                       xnode->nodeID, xnode->startHeight);

            SendNetMessage(xnode->nodeID, NetMsgType::GETHEADERS, xnode->sendVersion, 0,
                           cIndexManager.GetChain().GetLocator(pindexLast), uint256());
        }

        bool fCanDirectFetch =
                Tip()->GetBlockTime() > (GetAdjustedTime() - Params().GetConsensus().nPowTargetSpacing * 20);

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
                    !ifNetObj->DoseBlockInFlight(pindexWalk->GetBlockHash()) &&
                    (!IsWitnessEnabled(pindexWalk->pprev, Params().GetConsensus()) ||
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
                mlog_error("Large reorg, won't direct fetch to %s (%d)",
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
                    if (ifNetObj->MarkBlockInFlight(xnode->nodeID, pindex->GetBlockHash(), pindex))
                        xnode->nBlocksInFlight++;
                    mlog_notice("Requesting block %s from  peer=%d",
                                pindex->GetBlockHash().ToString(), xnode->nodeID);
                }
                if (vGetData.size() > 1)
                {
                    mlog_notice("Downloading blocks toward %s (%d) via headers direct fetch",
                                pindexLast->GetBlockHash().ToString(), pindexLast->nHeight);
                }
                if (vGetData.size() > 0)
                {
                    if (IsFlagsBitOn(xnode->flags, NF_DESIREDCMPCTVERSION) && vGetData.size() == 1 &&
                        ifNetObj->GetInFlightBlockCount() == 1 &&
                        pindexLast->pprev->IsValid(BLOCK_VALID_CHAIN))
                    {
                        // In any case, we want to download using a compact block, not a regular one
                        vGetData[0] = CInv(MSG_CMPCT_BLOCK, vGetData[0].hash);
                    }
                    SendNetMessage(xnode->nodeID, NetMsgType::GETDATA, xnode->sendVersion, 0, vGetData);
                }
            }
        }
    }

    return true;
}

bool CChainComponent::NetRequestBlockData(ExNode *xnode, uint256 blockHash, int blockType, void *filter)
{
    assert(xnode != nullptr);

    GET_NET_INTERFACE(ifNetObj);
    assert(ifNetObj != nullptr);

    bool isOK = false;
    const Consensus::Params &consensusParams = Params().GetConsensus();

    std::shared_ptr<const CBlock> a_recent_block;
    std::shared_ptr<const CBlockHeaderAndShortTxIDs> a_recent_compact_block;
    bool fWitnessesPresentInARecentCompactBlock;
    {
        LOCK(cs_most_recent_block);
        a_recent_block = most_recent_block;
        a_recent_compact_block = most_recent_compact_block;
        fWitnessesPresentInARecentCompactBlock = fWitnessesPresentInMostRecentCompactBlock;
    }

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
            CValidationState dummy;
            ActivateBestChain(dummy, Params(), a_recent_block);
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
            isOK = bi->IsValid(BLOCK_VALID_SCRIPTS) && (GetIndexBestHeader() != nullptr) &&
                   (GetIndexBestHeader()->GetBlockTime() - bi->GetBlockTime() < nOneMonth) &&
                   (GetBlockProofEquivalentTime(*GetIndexBestHeader(), *bi, *GetIndexBestHeader(),
                                                consensusParams) < nOneMonth);
            if (!isOK)
            {
                mlog_error("%s: ignoring request from peer=%i for old block that isn't in the main chain",
                           __func__, xnode->nodeID);
            }
        }
    }
    // disconnect node in case we have reached the outbound limit for serving historical blocks
    // never disconnect whitelisted nodes
    static const int nOneWeek = 7 * 24 * 60 * 60; // assume > 1 week = historical
    if (isOK && ifNetObj->OutboundTargetReached(true) && (((GetIndexBestHeader() != nullptr) &&
                                                           (GetIndexBestHeader()->GetBlockTime() -
                                                            bi->GetBlockTime() > nOneWeek)) ||
                                                          blockType == MSG_FILTERED_BLOCK) &&
        !IsFlagsBitOn(xnode->flags, NF_WHITELIST))
    {
        mlog_error("historical block serving limit reached, disconnect peer=%d\n",
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
        if (a_recent_block && a_recent_block->GetHash() == bi->GetBlockHash())
        {
            pblock = a_recent_block;
        } else
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
            if (filter)
            {
                CMerkleBlock merkleBlock = CMerkleBlock(*pblock, *(CBloomFilter *)filter);
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
            // If a peer is asking for old blocks, we're almost guaranteed
            // they won't have a useful mempool to match against a compact block,
            // and we don't feel like constructing the object for them, so
            // instead we respond with the full, non-compact block.
            bool fPeerWantsWitness = IsFlagsBitOn(xnode->flags, NF_WANTCMPCTWITNESS);
            int nSendFlags = fPeerWantsWitness ? 0 : SERIALIZE_TRANSACTION_NO_WITNESS;
            bool fCanDirectFetch = Tip()->GetBlockTime() > (GetAdjustedTime() - consensusParams.nPowTargetSpacing * 20);
            if (fCanDirectFetch &&
                bi->nHeight >= cIndexManager.GetChain().Height() - MAX_CMPCTBLOCK_DEPTH)
            {
                if ((fPeerWantsWitness || !fWitnessesPresentInARecentCompactBlock) &&
                    a_recent_compact_block &&
                    a_recent_compact_block->header.GetHash() == bi->GetBlockHash())
                {
                    SendNetMessage(xnode->nodeID, NetMsgType::MERKLEBLOCK, xnode->sendVersion, nSendFlags,
                                   *a_recent_compact_block);
                } else
                {
                    CBlockHeaderAndShortTxIDs cmpctblock(*pblock, fPeerWantsWitness);
                    SendNetMessage(xnode->nodeID, NetMsgType::CMPCTBLOCK, xnode->sendVersion, nSendFlags, cmpctblock);
                }
            } else
            {
                SendNetMessage(xnode->nodeID, NetMsgType::BLOCK, xnode->sendVersion, nSendFlags, *pblock);
            }
        }
    }
    return isOK;
}

bool CChainComponent::NetReceiveBlockData(ExNode *xnode, CDataStream &stream, uint256 &blockHash)
{
    assert(xnode != nullptr);

    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    stream >> *pblock;

    mlog_notice("received block %s peer=%d", pblock->GetHash().ToString(), xnode->nodeID);

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

bool CChainComponent::NetRequestBlockTxn(ExNode *xnode, CDataStream &stream)
{
    assert(xnode != nullptr);

    BlockTransactionsRequest req;
    stream >> req;

    std::shared_ptr<const CBlock> recent_block;
    {
        LOCK(cs_most_recent_block);
        if (most_recent_block_hash == req.blockhash)
            recent_block = most_recent_block;
        // Unlock cs_most_recent_block to avoid cs_main lock inversion
    }

    if (recent_block)
    {
        NetSendBlockTransactions(xnode, req, *recent_block);
        return true;
    }

    LOCK(cs_main);

    CBlockIndex *bi = cIndexManager.GetBlockIndex(req.blockhash);
    if (!bi || !(bi->nStatus & BLOCK_HAVE_DATA))
    {
        mlog_error("Peer %d sent us a getblocktxn for a block we don't have", xnode->nodeID);
        return true;
    }

    if (bi->nHeight < cIndexManager.GetChain().Height() - MAX_BLOCKTXN_DEPTH)
    {
        // If an older block is requested (should never happen in practice,
        // but can happen in tests) send a block response instead of a
        // blocktxn response. Sending a full block response instead of a
        // small blocktxn response is preferable in the case where a peer
        // might maliciously send lots of getblocktxn requests to trigger
        // expensive disk reads, because it will require the peer to
        // actually receive all the data read from disk over the network.
        mlog_error("Peer %d sent us a getblocktxn for a block > %i deep", xnode->nodeID, MAX_BLOCKTXN_DEPTH);
        int blockType = IsFlagsBitOn(xnode->flags, NF_WANTCMPCTWITNESS) ? MSG_WITNESS_BLOCK : MSG_BLOCK;
        NetRequestBlockData(xnode, req.blockhash, blockType, nullptr);
        return true;
    }

    CBlock block;
    bool ret = ReadBlockFromDisk(block, bi, Params().GetConsensus());
    assert(ret);

    return NetSendBlockTransactions(xnode, req, block);
}

bool CChainComponent::NetRequestMostRecentCmpctBlock(ExNode *xnode, uint256 bestBlockHint)
{
    assert(xnode != nullptr);

    int nSendFlags = IsFlagsBitOn(xnode->flags, NF_WANTCMPCTWITNESS) ? 0 : SERIALIZE_TRANSACTION_NO_WITNESS;

    LOCK(cs_most_recent_block);
    if (most_recent_block_hash == bestBlockHint)
    {
        if (IsFlagsBitOn(xnode->flags, NF_WANTCMPCTWITNESS) ||
            !fWitnessesPresentInMostRecentCompactBlock)
            SendNetMessage(xnode->nodeID, NetMsgType::CMPCTBLOCK, xnode->sendVersion, nSendFlags,
                           *most_recent_compact_block);
        else
        {
            CBlockHeaderAndShortTxIDs cmpctblock(*most_recent_block, IsFlagsBitOn(xnode->flags, NF_WANTCMPCTWITNESS));
            SendNetMessage(xnode->nodeID, NetMsgType::CMPCTBLOCK, xnode->sendVersion, nSendFlags, cmpctblock);
        }
        return true;
    }
    return false;
}

bool
CChainComponent::ProcessNewBlock(const std::shared_ptr<const CBlock> pblock, bool fForceProcessing, bool *fNewBlock)
{
    return ProcessNewBlock(Params(), pblock, fForceProcessing, fNewBlock);
}

bool CChainComponent::NetSendBlockTransactions(ExNode *xnode, const BlockTransactionsRequest &req, const CBlock &block)
{
    BlockTransactions resp(req);
    for (size_t i = 0; i < req.indexes.size(); i++)
    {
        if (req.indexes[i] >= block.vtx.size())
        {
            xnode->nMisbehavior = 100;
            mlog_error("Peer %d sent us a getblocktxn with out-of-bounds tx indices", xnode->nodeID);
            return false;
        }
        resp.txn[i] = block.vtx[req.indexes[i]];
    }

    int nSendFlags = IsFlagsBitOn(xnode->flags, NF_WANTCMPCTWITNESS) ? 0 : SERIALIZE_TRANSACTION_NO_WITNESS;
    return SendNetMessage(xnode->nodeID, NetMsgType::BLOCKTXN, xnode->sendVersion, nSendFlags, resp);
}