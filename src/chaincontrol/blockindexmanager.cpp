///////////////////////////////////////////////////////////
//  CBlockIndexManager.cpp
//  Implementation of the Class CBlockIndexManager
//  Created on:      30-1-2018 16:40:57
//  Original author: marco
///////////////////////////////////////////////////////////

#include "blockindexmanager.h"

CBlockIndexManager::CBlockIndexManager()
{

}

CBlockIndexManager::~CBlockIndexManager()
{

}

int CBlockIndexManager::FindMostWorkIndex()
{

    return 0;
}

CBlockIndex *CBlockIndexManager::AddToBlockIndex(const CBlockHeader &block)
{

    return NULL;
}


void CBlockIndexManager::SortBlockIndex()
{
    if (mBlockIndex.empty())
    {
        return;
    }

    std::vector<std::pair<int, CBlockIndex *> > vecSortedByHeight;
    vecSortedByHeight.reserve(mBlockIndex.size());

    for (const std::pair<uint256, CBlockIndex *> &item: mBlockIndex)
    {
        CBlockIndex *pIndex = item.second;
        vecSortedByHeight.push_back(std::make_pair(pIndex->nHeight, pIndex));
    }

    sort(vecSortedByHeight.begin(), vecSortedByHeight.end());

    for (const std::pair<int, CBlockIndex *> &item: vecSortedByHeight)
    {
        CBlockIndex *pIndex = item.second;
        pIndex->nChainWork = (pIndex->pprev ? pIndex->pprev->nChainWork : 0) + GetBlockProof(*pIndex);
        pIndex->nTimeMax = (pIndex->pprev ? std::max(pIndex->pprev->nTimeMax, pIndex->nTime) : pIndex->nTime);
        // We can link the chain of blocks for which we've received transactions at some point.
        // Pruned nodes may have deleted the block.
        if (pIndex->nTx > 0)
        {
            if (pIndex->pprev)
            {
                if (pIndex->pprev->nChainTx)
                {
                    pIndex->nChainTx = pIndex->pprev->nChainTx + pIndex->nTx;
                } else
                {
                    pIndex->nChainTx = 0;
                    mBlocksUnlinked.insert(std::make_pair(pIndex->pprev, pIndex));
                }
            } else
            {
                pIndex->nChainTx = pIndex->nTx;
            }
        }
        if (!(pIndex->nStatus & BLOCK_FAILED_MASK) && pIndex->pprev && (pIndex->pprev->nStatus & BLOCK_FAILED_MASK))
        {
            pIndex->nStatus |= BLOCK_FAILED_CHILD;
            setDirtyBlockIndex.insert(pIndex);
        }
        if (pIndex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pIndex->nChainTx || pIndex->pprev == nullptr))
            setBlockIndexCandidates.insert(pIndex);
        if (pIndex->nStatus & BLOCK_FAILED_MASK &&
            (!pIndexBestInvalid || pIndex->nChainWork > pIndexBestInvalid->nChainWork))
            pIndexBestInvalid = pIndex;
        if (pIndex->pprev)
            pIndex->BuildSkip();
        if (pIndex->IsValid(BLOCK_VALID_TREE) &&
            (pIndexBestHeader == nullptr || CBlockIndexWorkComparator()(pIndexBestHeader, pIndex)))
            pIndexBestHeader = pIndex;
    }
}

bool CBlockIndexManager::Init()
{
    UnLoadBlockIndex();

    CArgsManager *cArgs = app().GetArgsManager();
    bReIndex = cArgs->GetArg<bool>("-reindex", false);
    int64_t iTotalCache = (cArgs->GetArg<int64_t>("-dbcache", nDefaultDbCache)) << 20;
    iTotalCache = std::max(iTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    iTotalCache = std::min(iTotalCache, nMaxDbCache << 20); // total cache cannot be greater than nMaxDbcache
    int64_t iBlockTreeDBCache = iTotalCache / 8;
    iBlockTreeDBCache = std::min(iBlockTreeDBCache,
                                 (cArgs->GetArg<bool>("-txindex", DEFAULT_TXINDEX) ? nMaxBlockDBAndTxIndexCache
                                                                                   : nMaxBlockDBCache) << 20);
    pBlcokTreee = std::unique_ptr<CBlockTreeDB>(new CBlockTreeDB(iBlockTreeDBCache, false, bReIndex));

    LoadBlockIndex();

    return true;
}

CBlockIndex *CBlockIndexManager::InsertBlockIndex(uint256 hash)
{
    if (hash.IsNull())
        return nullptr;

    // Return existing
    BlockMap::iterator mi = mBlockIndex.find(hash);
    if (mi != mBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex *pIndexNew = new CBlockIndex();
    if (!pIndexNew)
        throw std::runtime_error(std::string(__func__) + ": new CBlockIndex failed");
    mi = mBlockIndex.insert(std::make_pair(hash, pIndexNew)).first;
    pIndexNew->phashBlock = &((*mi).first);

    return pIndexNew;
}

bool CBlockIndexManager::LoadBlockIndex()
{
    CArgsManager *cArgs = app().GetArgsManager();
    bReIndex = cArgs->GetArg<bool>("-reindex", false);
    if (!bReIndex)
    {
        bool ret = LoadBlockIndexDB();
        if (!ret)
        {
            return false;
        }
    }

    if (bReIndex || mBlockIndex.empty())
    {
        bTxIndex = cArgs->GetArg<bool>("-txindex", DEFAULT_TXINDEX);
        pBlcokTreee->WriteFlag("txindex", bTxIndex);
    }

    return true;
}

void CBlockIndexManager::UnLoadBlockIndex()
{
    setBlockIndexCandidates.clear();
    pIndexBestInvalid = nullptr;
    pindexBestHeader = nullptr;
    mBlocksUnlinked.clear();
    vecBlockFileInfo.clear();
    iLastBlockFile = 0;
    setDirtyBlockIndex.clear();

    for (BlockMap::value_type &entry : mBlockIndex)
    {
        delete entry.second;
    }
    mBlockIndex.clear();
    bHavePruned = false;
}

void CBlockIndexManager::ReadBlockFileInfo()
{
    pBlcokTreee->ReadLastBlockFile(iLastBlockFile);
    vecBlockFileInfo.resize(iLastBlockFile + 1);
    for (int i = 0; i <= iLastBlockFile; i++)
    {
        pBlcokTreee->ReadBlockFileInfo(i, vecBlockFileInfo[i]);
    }
    for (int i = iLastBlockFile + 1; true; i++)
    {
        CBlockFileInfo info;
        if (pBlcokTreee->ReadBlockFileInfo(i, info))
        {
            vecBlockFileInfo.push_back(info);
        } else
        {
            break;
        }
    }
}

bool CBlockIndexManager::CheckBlockFileExist()
{
    std::set<int> setBlkDataFiles;
    for (const std::pair<uint256, CBlockIndex *> &item : mBlockIndex)
    {
        CBlockIndex *pIndex = item.second;
        if (pIndex->nStatus & BLOCK_HAVE_DATA)
        {
            setBlkDataFiles.insert(pIndex->nFile);
        }
    }

    for (std::set<int>::iterator it = setBlkDataFiles.begin(); it != setBlkDataFiles.end(); it++)
    {
        CDiskBlockPos pos(*it, 0);
        if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull())
        {
            return false;
        }
    }

    return true;
}

bool CBlockIndexManager::LoadBlockIndexDB()
{
    const Consensus::Params &consensus = app().GetChainParams()->GetConsensus();
    if (!pBlcokTreee->LoadBlockIndexGuts(consensus,
                                         std::bind(&CBlockIndexManager::InsertBlockIndex, this, std::placeholders::_1)))
    {
        return false;
    }

    SortBlockIndex();

    ReadBlockFileInfo();

    if (!CheckBlockFileExist())
    {
        return false;
    }

    pBlcokTreee->ReadFlag("prunedblockfiles", bHavePruned);
    if (bHavePruned)
    {
        LogPrintf("LoadBlockIndexDB(): Block files have previously been pruned\n");
    }

    bool bReIndexing = false;
    pBlcokTreee->ReadReindexing(bReIndexing);
    bReIndex |= bReIndexing;

    pBlcokTreee->ReadFlag("txindex", bTxIndex);
    LogPrintf("transaction index %s\n", bTxIndex ? "enabled" : "disabled");

    return true;
}


void CBlockIndexManager::PruneBlockIndexCandidates()
{

}