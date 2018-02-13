///////////////////////////////////////////////////////////
//  CBlockIndexManager.h
//  Implementation of the Class CBlockIndexManager
//  Created on:      30-1-2018 16:40:57
//  Original author: marco
///////////////////////////////////////////////////////////

#ifndef __SBTC_BLOCKINDEXMANAGER_H__
#define __SBTC_BLOCKINDEXMANAGER_H__

#include <atomic>
#include <exception>
#include <map>
#include <stdint.h>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>

#include "framework/base.hpp"
#include "sbtccore/transaction/txdb.h"
#include "sbtccore/block/validation.h"
#include "config/chainparamsbase.h"
#include "config/chainparams.h"
#include "config/argmanager.h"

using namespace appbase;

struct CBlockIndexWorkComparator
{
    bool operator()(const CBlockIndex *pa, const CBlockIndex *pb) const
    {
        // First sort by most total work, ...
        if (pa->nChainWork > pb->nChainWork)
            return false;
        if (pa->nChainWork < pb->nChainWork)
            return true;

        // ... then by earliest time received, ...
        if (pa->nSequenceId < pb->nSequenceId)
            return false;
        if (pa->nSequenceId > pb->nSequenceId)
            return true;

        // Use pointer address as tie breaker (should only happen with blocks
        // loaded from disk, as those all have id 0).
        if (pa < pb)
            return false;
        if (pa > pb)
            return true;

        // Identical blocks.
        return false;
    }
};

enum ResultBlockIndex
{
    OK_BLOCK_INDEX = 0,
    ERR_LOAD_INDEX_DB = -2000,
    ERR_LOAD_GENESIS,
    ERR_INIT_GENESIS,
    ERR_TXINDEX_STATE,
    ERR_PRUNE_STATE,
};

class CBlockIndexManager
{

public:
    CBlockIndexManager();

    virtual ~CBlockIndexManager();

    CBlockIndex *AddToBlockIndex(const CBlockHeader &block);

    CBlockIndex *FindMostWorkIndex();

    int LoadBlockIndex(int64_t iBlockTreeDBCache, bool bReset, const CChainParams &chainparams);

    void PruneBlockIndexCandidates();

    const CBlockIndex *LastCommonAncestor(const uint256 hasha, const uint256 hashb);

    const CBlockIndex *LastCommonAncestor(const CBlockIndex *pa, const CBlockIndex *pb);

    CBlockIndex *GetBlockIndex(const uint256 hash);

    CChain &GetChain();

    bool Flush();

    int GetLastBlockFile();

    std::vector<CBlockFileInfo> GetBlockFileInfo();

    void InvalidChainFound(CBlockIndex *pIndexNew);

    void InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state);

    void CheckBlockIndex(const Consensus::Params &consensusParams);

    void InvalidateBlock(CBlockIndex *pIndex, CBlockIndex *pInvalidWalkTip, bool bIndexWasInChain);

    void CheckForkWarningConditionsOnNewFork(CBlockIndex *pindexNewForkTip);

    void CheckForkWarningConditions();

    bool NeedRewind(const int height, const Consensus::Params &params);

    void RewindBlockIndex(const Consensus::Params &params);

    bool IsOnlyGenesisBlockIndex();

    bool NeedInitGenesisBlock(const CChainParams &chainparams);

    bool AcceptBlockHeader(const CBlockHeader &block, CValidationState &state, const CChainParams &chainparams,
                           CBlockIndex **ppindex);

    bool FindBlockPos(CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight,
                      uint64_t nTime, bool fKnown = false);

    CBlockIndex *GetIndexBestHeader();

    bool SetDirtyIndex(CBlockIndex *pIndex);

    bool ReceivedBlockTransactions(const CBlock &block, CValidationState &state, CBlockIndex *pindexNew,
                                   const CDiskBlockPos &pos, const Consensus::Params &consensusParams);

private:
    bool bReIndex = false;
    bool bTxIndex = false;
    bool bHavePruned = false;
    bool bPruneMode = false;
    int iLastBlockFile = 0;
    std::vector<CBlockFileInfo> vecBlockFileInfo;
    std::unordered_map<uint256, CBlockIndex *, BlockHasher> mBlockIndex;
    std::multimap<CBlockIndex *, CBlockIndex *> mBlocksUnlinked;
    std::unique_ptr<CBlockTreeDB> pBlcokTreee;
    std::set<CBlockIndex *, CBlockIndexWorkComparator> setBlockIndexCandidates;
    /** Dirty block index entries. */
    std::set<CBlockIndex *> setDirtyBlockIndex;
    /** Dirty block file entries. */
    std::set<int> setDirtyFileInfo;
    std::set<CBlockIndex *> setFailedBlocks;
    CBlockIndex *pIndexBestInvalid;
    CBlockIndex *pIndexBestHeader = nullptr;
    CBlockIndex *pIndexBestForkTip = nullptr;
    CBlockIndex *pIndexBestForkBase = nullptr;

    CChain cChainActive;

    CCriticalSection cs;

    bool Init(int64_t iBlockTreeDBCache, bool bReIndex, const CChainParams &chainparams);

    int Check(const CChainParams &chainparams);

    CBlockIndex *InsertBlockIndex(uint256 hash);

    void SortBlockIndex();

    bool CheckBlockFileExist();

    void ReadBlockFileInfo();

    bool LoadBlockIndexDB(const CChainParams &chainparams);

    void UnLoadBlockIndex();

    bool IsWitnessEnabled(const CBlockIndex *pindexPrev, const Consensus::Params &params);

    bool CheckBlockHeader(const CBlockHeader &block, CValidationState &state, const Consensus::Params &consensusParams,
                          bool fCheckPOW = true);

    bool ContextualCheckBlockHeader(const CBlockHeader &block, CValidationState &state, const CChainParams &params,
                                    const CBlockIndex *pindexPrev, int64_t nAdjustedTime);
};

#endif // !defined(__SBTC_BLOCKINDEXMANAGER_H__)
