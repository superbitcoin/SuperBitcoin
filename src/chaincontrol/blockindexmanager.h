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

class CBlockIndexManager
{

public:
    CBlockIndexManager();

    virtual ~CBlockIndexManager();

    CBlockIndex *AddToBlockIndex(const CBlockHeader &block);

    CBlockIndex *FindMostWorkIndex();

    bool Init(int64_t iBlockTreeDBCache, bool bReIndex);

    void PruneBlockIndexCandidates();

    const CBlockIndex *LastCommonAncestor(const uint256 hasha, const uint256 hashb);

    const CBlockIndex *LastCommonAncestor(const CBlockIndex *pa, const CBlockIndex *pb);
    
    const CBlockIndex *getBlockIndex(const uint256 hash);

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
    CBlockIndex *pIndexBestInvalid;
    CBlockIndex *pIndexBestHeader = nullptr;

    CBlockIndex *InsertBlockIndex(uint256 hash);

    void SortBlockIndex();

    bool CheckBlockFileExist();

    void ReadBlockFileInfo();

    bool LoadBlockIndex();

    bool LoadBlockIndexDB();

    void UnLoadBlockIndex();

};

#endif // !defined(__SBTC_BLOCKINDEXMANAGER_H__)
