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

#include "sbtcd/baseimpl.hpp"
#include "sbtccore/transaction/txdb.h"
#include "sbtccore/block/validation.h"
#include "config/chainparams.h"
#include "config/argmanager.h"

using namespace appbase;

struct BlockHasher
{
    size_t operator()(const uint256 &hash) const
    {
        return hash.GetCheapHash();
    }
};

typedef std::unordered_map<uint256, CBlockIndex *, BlockHasher> BlockMap;

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight
{
    bool operator()(const CBlockIndex *a, const CBlockIndex *b) const
    {
        /* Make sure that unequal blocks with the same height do not compare
           equal. Use the pointers themselves to make a distinction. */

        if (a->nHeight != b->nHeight)
            return (a->nHeight > b->nHeight);

        return a < b;
    }
};

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

    static CBlockIndexManager &Instance();

    CBlockIndex *AddToBlockIndex(const CBlockHeader &block);

    CBlockIndex *FindForkInGlobalIndex(const CChain &chain, const CBlockLocator &locator);

    CBlockIndex *FindMostWorkIndex();

    int LoadBlockIndex(const Consensus::Params &consensus, int64_t iBlockTreeDBCache, bool bReset, bool txIndex);

    void PruneBlockIndexCandidates();

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

    bool IsReIndexing();

    bool IsTxIndex();

    bool NeedInitGenesisBlock(const CChainParams &chainparams);

    bool AcceptBlockHeader(const CBlockHeader &block, CValidationState &state, const CChainParams &chainparams,
                           CBlockIndex **ppindex);

    bool FindBlockPos(CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight,
                      uint64_t nTime, bool fKnown = false);

    bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize);

    CBlockIndex *GetIndexBestHeader();

    bool SetDirtyIndex(CBlockIndex *pIndex);

    bool ReceivedBlockTransactions(const CBlock &block, CValidationState &state, CBlockIndex *pindexNew,
                                   const CDiskBlockPos &pos, const Consensus::Params &consensusParams);

    bool CheckBlockHeader(const CBlockHeader &block, CValidationState &state, const Consensus::Params &consensusParams,
                          bool fCheckPOW = true);

    bool ContextualCheckBlockHeader(const CBlockHeader &block, CValidationState &state, const CChainParams &params,
                                    const CBlockIndex *pindexPrev, int64_t nAdjustedTime);

    void PreciousBlock(CValidationState &state, const CChainParams &params, CBlockIndex *pindex);

    bool ResetBlockFailureFlags(CBlockIndex *pindex);

    int64_t GetBlockProofEquivalentTime(uint256 hashAssumeValid, const CBlockIndex *pindex, const CChainParams &params);

    std::set<const CBlockIndex *, CompareBlocksByHeight> GetTips();

    CBlockTreeDB *GetBlockTreeDB();

private:
    bool bReIndexing = false;
    bool bTxIndex = false;
    bool bHavePruned = false;
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

    /** Best header we've seen so far (used for getheaders queries' starting points). */
    CBlockIndex *pIndexBestHeader = nullptr;
    CBlockIndex *pIndexBestForkTip = nullptr;
    CBlockIndex *pIndexBestForkBase = nullptr;

    CChain cChainActive;

    bool fCheckBlockIndex = false;

    CCriticalSection cs;
    /**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
    CCriticalSection cs_nBlockSequenceId;
    /** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
    int32_t nBlockSequenceId = 1;
    /** Decreasing counter (used by subsequent preciousblock calls). */
    int32_t nBlockReverseSequenceId = -1;
    /** chainwork for the last block that preciousblock has been applied to. */
    arith_uint256 nLastPreciousChainwork = 0;

    bool Init(int64_t iBlockTreeDBCache, bool bReIndex);

    int Check(const uint256 hashGenesisBlock);

    CBlockIndex *InsertBlockIndex(uint256 hash);

    void SortBlockIndex();

    bool CheckBlockFileExist();

    void ReadBlockFileInfo();

    bool LoadBlockIndexDB(const Consensus::Params &consensus);

    void UnLoadBlockIndex();

    //! Returns last CBlockIndex* in mapBlockIndex that is a checkpoint
    CBlockIndex *GetLastCheckpoint(const CCheckpointData &data);

    CBlockIndex const *GetLastCheckPointBlockIndex(const CCheckpointData &data);

    bool IsAgainstCheckPoint(const CChainParams &chainparams, const CBlockIndex *pindex);

    bool IsAgainstCheckPoint(const CChainParams &chainparams, const int &nHeight, const uint256 &hash);

public:
    static log4cpp::Category &mlog;
};

#endif // !defined(__SBTC_BLOCKINDEXMANAGER_H__)
