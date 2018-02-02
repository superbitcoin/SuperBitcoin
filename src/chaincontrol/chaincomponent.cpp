#include <iostream>
#include "chaincomponent.h"

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

bool CChainCommonent::ReplayBlocks()
{
    CChainParams *params = appbase::app().GetChainParams();

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
            if (!ReadBlockFromDisk(block, pIndexOld, params->GetConsensus()))
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
        if (!ReadBlockFromDisk(block, pIndex, params->GetConsensus()))
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