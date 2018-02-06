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

int CBlockIndexManager::GetLastBlockFile()
{
    return iLastBlockFile;
}

std::vector<CBlockFileInfo> CBlockIndexManager::GetBlockFileInfo()
{
    return vecBlockFileInfo;
}

CBlockIndex *CBlockIndexManager::FindMostWorkIndex()
{
    do
    {
        CBlockIndex *pIndexNew = nullptr;

        // Find the best candidate header.
        {
            std::set<CBlockIndex *, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
            {
                return nullptr;
            }
            pIndexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        CBlockIndex *pIndexTest = pIndexNew;
        bool bInvalidAncestor = false;
        while (pIndexTest && !cChainActive.Contains(pIndexTest))
        {
            assert(pIndexTest->nChainTx || pIndexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            bool bFailedChain = pIndexTest->nStatus & BLOCK_FAILED_MASK;
            bool bMissingData = !(pIndexTest->nStatus & BLOCK_HAVE_DATA);
            if (bFailedChain || bMissingData)
            {
                // Candidate chain is not usable (either invalid or missing data)
                if (bFailedChain &&
                    (pIndexBestInvalid == nullptr || pIndexNew->nChainWork > pIndexBestInvalid->nChainWork))
                    pIndexBestInvalid = pIndexNew;
                CBlockIndex *pindexFailed = pIndexNew;
                // Remove the entire chain from the set.
                while (pIndexTest != pindexFailed)
                {
                    if (bFailedChain)
                    {
                        pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                    } else if (bMissingData)
                    {
                        // If we're missing data, then add back to mapBlocksUnlinked,
                        // so that if the block arrives in the future we can try adding
                        // to setBlockIndexCandidates again.
                        mBlocksUnlinked.insert(std::make_pair(pindexFailed->pprev, pindexFailed));
                    }
                    setBlockIndexCandidates.erase(pindexFailed);
                    pindexFailed = pindexFailed->pprev;
                }
                setBlockIndexCandidates.erase(pIndexTest);
                bInvalidAncestor = true;
                break;
            }
            pIndexTest = pIndexTest->pprev;
        }

        if (!bInvalidAncestor)
        {
            return pIndexNew;
        }
    } while (true);
}

CBlockIndex *CBlockIndexManager::AddToBlockIndex(const CBlockHeader &block)
{
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mBlockIndex.find(hash);
    if (it != mBlockIndex.end())
        return it->second;

    // Construct new block index object
    CBlockIndex *pIndexNew = new CBlockIndex(block);
    assert(pIndexNew);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pIndexNew->nSequenceId = 0;
    BlockMap::iterator mi = mBlockIndex.insert(std::make_pair(hash, pIndexNew)).first;
    pIndexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = mBlockIndex.find(block.hashPrevBlock);
    if (miPrev != mBlockIndex.end())
    {
        pIndexNew->pprev = (*miPrev).second;
        pIndexNew->nHeight = pIndexNew->pprev->nHeight + 1;
        pIndexNew->BuildSkip();
    }
    pIndexNew->nTimeMax = (pIndexNew->pprev ? std::max(pIndexNew->pprev->nTimeMax, pIndexNew->nTime)
                                            : pIndexNew->nTime);
    pIndexNew->nChainWork = (pIndexNew->pprev ? pIndexNew->pprev->nChainWork : 0) + GetBlockProof(*pIndexNew);
    pIndexNew->RaiseValidity(BLOCK_VALID_TREE);
    if (pIndexBestHeader == nullptr || pIndexBestHeader->nChainWork < pIndexNew->nChainWork)
        pIndexBestHeader = pIndexNew;

    setDirtyBlockIndex.insert(pIndexNew);

    return pIndexNew;
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

bool CBlockIndexManager::Init(int64_t iBlockTreeDBCache, bool bReIndex)
{
    std::cout << "initialize index manager \n";

    UnLoadBlockIndex();

    pBlcokTreee = std::unique_ptr<CBlockTreeDB>(new CBlockTreeDB(iBlockTreeDBCache, false, bReIndex));

    if (bReIndex)
    {
        pBlcokTreee->WriteReindexing(true);
    }

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
        const CArgsManager &cArgs = app().GetArgsManager();
        bTxIndex = cArgs.GetArg<bool>("-txindex", DEFAULT_TXINDEX);
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
    const Consensus::Params &consensus = app().GetChainParams().GetConsensus();
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

/** Find the last common ancestor two blocks have.
 *  Both pa and pb must be non-nullptr. */
const CBlockIndex *CBlockIndexManager::LastCommonAncestor(const CBlockIndex *pa, const CBlockIndex *pb)
{
    if (pa->nHeight > pb->nHeight)
    {
        pa = pa->GetAncestor(pb->nHeight);
    } else if (pb->nHeight > pa->nHeight)
    {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb)
    {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}

/** Find the last common ancestor two blocks have.
 *
 * @param hasha
 * @param hashb
 * @return
 */
const CBlockIndex *CBlockIndexManager::LastCommonAncestor(const uint256 hashA, const uint256 hashB)
{
    const CBlockIndex *pIndexA;
    const CBlockIndex *pIndexB;
    const CBlockIndex *pIndexFork;

    if (mBlockIndex.count(hashA) == 0 || mBlockIndex.count(hashB) == 0)
    {
        error("LastCommonAncestor(): reorganization to unknown block requested");
        return nullptr;
    }

    pIndexA = mBlockIndex[hashA];
    pIndexB = mBlockIndex[hashB];
    pIndexFork = LastCommonAncestor(pIndexA, pIndexB);

    assert(pIndexFork != nullptr);

    return pIndexFork;
}

CBlockIndex *CBlockIndexManager::GetBlockIndex(const uint256 hash)
{
    if (mBlockIndex.count(hash) == 0)
    {
        error("LastCommonAncestor(): reorganization to unknown block requested");
        return nullptr;
    }

    return mBlockIndex[hash];
}

CChain &CBlockIndexManager::GetChain()
{
    return cChainActive;
}

bool CBlockIndexManager::Flush()
{
    return true;
}