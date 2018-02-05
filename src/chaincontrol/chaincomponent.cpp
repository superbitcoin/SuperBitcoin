#include <iostream>
#include "chaincomponent.h"
#include "sbtccore/streams.h"
#include "interface/inetcomponent.h"
#include "utils/net/netmessagehelper.h"

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

int CChainCommonent::GetActiveChainHeight() const
{
    //TODO:
    return 0;
}

bool CChainCommonent::NetGetCheckPoint(XNodeInfo* nodeInfo, int height)
{
    if (!nodeInfo)
        return false;

    std::set<int>& checkpointKnown = m_nodeCheckPointKnown[nodeInfo->nodeID];

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
        SendNetMessage(nodeInfo->nodeID, NetMsgType::CHECKPOINT, nodeInfo->sendVersion, 0, vSendData);
    }

    return true;
}

bool CChainCommonent::NetCheckPoint(XNodeInfo* nodeInfo, CDataStream& stream)
{
    if (!nodeInfo)
        return false;

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
                m_nodeCheckPointKnown[nodeInfo->nodeID].insert(point.getHeight());
                vIndex.push_back(point);
            }
        }
        else
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
        SendNetMessage(-1, NetMsgType::CHECKPOINT, nodeInfo->sendVersion, 0, vdata);
    }

    return true;
}

bool CChainCommonent::NetGetBlocks(XNodeInfo* nodeInfo, CDataStream& stream, std::vector<uint256>& blockHashes)
{
    if (!nodeInfo)
        return false;

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
        pindex = chainActive.Next(pindex);

    int nLimit = 500;
    LogPrint(BCLog::NET, "getblocks %d to %s limit %d from peer=%d\n", (pindex ? pindex->nHeight : -1),
             hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, nodeInfo->nodeID);

    const CChainParams &chainparams = appbase::app().GetChainParams();
    for (; pindex; pindex = chainActive.Next(pindex))
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
                           pindex->nHeight <= chainActive.Tip()->nHeight - nPrunedBlocksLikelyToHave))
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

bool CChainCommonent::ReplayBlocks()
{
    const CChainParams &params = appbase::app().GetChainParams();

    CCoinsView *view(cViewManager.getCoinViewDB());
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

    pIndexNew = cIndexManager.getBlockIndex(vecHashHeads[0]);
    if (!pIndexNew)
    {
        return false;
    }

    if (!vecHashHeads[1].IsNull())
    {
        pIndexOld = cIndexManager.getBlockIndex(vecHashHeads[1]);
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