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
    pIndexBestHeader = nullptr;
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

void CBlockIndexManager::InvalidChainFound(CBlockIndex *pIndexNew)
{
    if (!pIndexBestInvalid || pIndexNew->nChainWork > pIndexBestInvalid->nChainWork)
        pIndexBestInvalid = pIndexNew;

    LogPrintf("%s: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
              pIndexNew->GetBlockHash().ToString(), pIndexNew->nHeight,
              log(pIndexNew->nChainWork.getdouble()) / log(2.0), DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
                                                                                   pIndexNew->GetBlockTime()));
    CBlockIndex *tip = cChainActive.Tip();
    assert (tip);
    LogPrintf("%s:  current best=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
              tip->GetBlockHash().ToString(), cChainActive.Height(), log(tip->nChainWork.getdouble()) / log(2.0),
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime()));
    //    CheckForkWarningConditions();
}

void CBlockIndexManager::CheckBlockIndex(const Consensus::Params &consensusParams)
{
    if (!fCheckBlockIndex)
    {
        return;
    }

    LOCK(cs);

    // During a reindex, we read the genesis block and call CheckBlockIndex before ActivateBestChain,
    // so we have the genesis block in mapBlockIndex but no active chain.  (A few of the tests when
    // iterating the block tree require that chainActive has been initialized.)
    if (cChainActive.Height() < 0)
    {
        assert(mBlockIndex.size() <= 1);
        return;
    }

    // Build forward-pointing map of the entire block tree.
    std::multimap<CBlockIndex *, CBlockIndex *> forward;
    for (BlockMap::iterator it = mBlockIndex.begin(); it != mBlockIndex.end(); it++)
    {
        forward.insert(std::make_pair(it->second->pprev, it->second));
    }

    assert(forward.size() == mBlockIndex.size());

    std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator, std::multimap<CBlockIndex *, CBlockIndex *>::iterator> rangeGenesis = forward.equal_range(
            nullptr);
    CBlockIndex *pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    assert(rangeGenesis.first == rangeGenesis.second); // There is only one index entry with parent nullptr.

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex *pindexFirstInvalid = nullptr; // Oldest ancestor of pindex which is invalid.
    CBlockIndex *pindexFirstMissing = nullptr; // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    CBlockIndex *pindexFirstNeverProcessed = nullptr; // Oldest ancestor of pindex for which nTx == 0.
    CBlockIndex *pindexFirstNotTreeValid = nullptr; // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    CBlockIndex *pindexFirstNotTransactionsValid = nullptr; // Oldest ancestor of pindex which does not have BLOCK_VALID_TRANSACTIONS (regardless of being valid or not).
    CBlockIndex *pindexFirstNotChainValid = nullptr; // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    CBlockIndex *pindexFirstNotScriptsValid = nullptr; // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).
    while (pindex != nullptr)
    {
        nNodes++;
        if (pindexFirstInvalid == nullptr && pindex->nStatus & BLOCK_FAILED_VALID)
            pindexFirstInvalid = pindex;
        if (pindexFirstMissing == nullptr && !(pindex->nStatus & BLOCK_HAVE_DATA))
            pindexFirstMissing = pindex;
        if (pindexFirstNeverProcessed == nullptr && pindex->nTx == 0)
            pindexFirstNeverProcessed = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotTreeValid == nullptr &&
            (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE)
            pindexFirstNotTreeValid = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotTransactionsValid == nullptr &&
            (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TRANSACTIONS)
            pindexFirstNotTransactionsValid = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotChainValid == nullptr &&
            (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN)
            pindexFirstNotChainValid = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotScriptsValid == nullptr &&
            (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS)
            pindexFirstNotScriptsValid = pindex;

        // Begin: actual consistency checks.
        if (pindex->pprev == nullptr)
        {
            // Genesis block checks.
            assert(pindex->GetBlockHash() == consensusParams.hashGenesisBlock); // Genesis block's hash must match.
            assert(pindex == cChainActive.Genesis()); // The current active chain's genesis block must be this block.
        }
        if (pindex->nChainTx == 0)
            assert(pindex->nSequenceId <=
                   0);  // nSequenceId can't be set positive for blocks that aren't linked (negative is used for preciousblock)
        // VALID_TRANSACTIONS is equivalent to nTx > 0 for all nodes (whether or not pruning has occurred).
        // HAVE_DATA is only equivalent to nTx > 0 (or VALID_TRANSACTIONS) if no pruning has occurred.
        if (!bHavePruned)
        {
            // If we've never pruned, then HAVE_DATA should be equivalent to nTx > 0
            assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
            assert(pindexFirstMissing == pindexFirstNeverProcessed);
        } else
        {
            // If we have pruned, then we can only say that HAVE_DATA implies nTx > 0
            if (pindex->nStatus & BLOCK_HAVE_DATA)
                assert(pindex->nTx > 0);
        }
        if (pindex->nStatus & BLOCK_HAVE_UNDO)
            assert(pindex->nStatus & BLOCK_HAVE_DATA);
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) ==
               (pindex->nTx > 0)); // This is pruning-independent.
        // All parents having had data (at some point) is equivalent to all parents being VALID_TRANSACTIONS, which is equivalent to nChainTx being set.
        assert((pindexFirstNeverProcessed != nullptr) == (pindex->nChainTx ==
                                                          0)); // nChainTx != 0 is used to signal that all parent blocks have been processed (but may have been pruned).
        assert((pindexFirstNotTransactionsValid != nullptr) == (pindex->nChainTx == 0));
        assert(pindex->nHeight == nHeight); // nHeight must be consistent.
        assert(pindex->pprev == nullptr || pindex->nChainWork >=
                                           pindex->pprev->nChainWork); // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight <
                                                 nHeight))); // The pskip pointer must point back for all but the first 2 blocks.
        assert(pindexFirstNotTreeValid == nullptr); // All mapBlockIndex entries must at least be TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE)
            assert(pindexFirstNotTreeValid == nullptr); // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN)
            assert(pindexFirstNotChainValid == nullptr); // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS)
            assert(pindexFirstNotScriptsValid == nullptr); // SCRIPTS valid implies all parents are SCRIPTS valid
        if (pindexFirstInvalid == nullptr)
        {
            // Checks for not-invalid blocks.
            assert((pindex->nStatus & BLOCK_FAILED_MASK) ==
                   0); // The failed mask cannot be set for blocks without invalid parents.
        }
        if (!CBlockIndexWorkComparator()(pindex, cChainActive.Tip()) && pindexFirstNeverProcessed == nullptr)
        {
            if (pindexFirstInvalid == nullptr)
            {
                // If this block sorts at least as good as the current tip and
                // is valid and we have all data for its parents, it must be in
                // setBlockIndexCandidates.  chainActive.Tip() must also be there
                // even if some data has been pruned.
                if (pindexFirstMissing == nullptr || pindex == cChainActive.Tip())
                {
                    assert(setBlockIndexCandidates.count(pindex));
                }
                // If some parent is missing, then it could be that this block was in
                // setBlockIndexCandidates but had to be removed because of the missing data.
                // In this case it must be in mapBlocksUnlinked -- see test below.
            }
        } else
        { // If this block sorts worse than the current tip or some ancestor's block has never been seen, it cannot be in setBlockIndexCandidates.
            assert(setBlockIndexCandidates.count(pindex) == 0);
        }
        // Check whether this block is in mapBlocksUnlinked.
        std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator, std::multimap<CBlockIndex *, CBlockIndex *>::iterator> rangeUnlinked = mBlocksUnlinked.equal_range(
                pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second)
        {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex)
            {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed != nullptr &&
            pindexFirstInvalid == nullptr)
        {
            // If this block has block data available, some parent was never received, and has no invalid parents, it must be in mapBlocksUnlinked.
            assert(foundInUnlinked);
        }
        if (!(pindex->nStatus & BLOCK_HAVE_DATA))
            assert(!foundInUnlinked); // Can't be in mapBlocksUnlinked if we don't HAVE_DATA
        if (pindexFirstMissing == nullptr)
            assert(!foundInUnlinked); // We aren't missing data for any parent -- cannot be in mapBlocksUnlinked.
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed == nullptr &&
            pindexFirstMissing != nullptr)
        {
            // We HAVE_DATA for this block, have received data for all parents at some point, but we're currently missing data for some parent.
            assert(bHavePruned); // We must have pruned.
            // This block may have entered mapBlocksUnlinked if:
            //  - it has a descendant that at some point had more work than the
            //    tip, and
            //  - we tried switching to that descendant but were missing
            //    data for some intermediate block between chainActive and the
            //    tip.
            // So if this block is itself better than chainActive.Tip() and it wasn't in
            // setBlockIndexCandidates, then it must be in mapBlocksUnlinked.
            if (!CBlockIndexWorkComparator()(pindex, cChainActive.Tip()) && setBlockIndexCandidates.count(pindex) == 0)
            {
                if (pindexFirstInvalid == nullptr)
                {
                    assert(foundInUnlinked);
                }
            }
        }
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash()); // Perhaps too slow
        // End: actual consistency checks.

        // Try descending into the first subnode.
        std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator, std::multimap<CBlockIndex *, CBlockIndex *>::iterator> range = forward.equal_range(
                pindex);
        if (range.first != range.second)
        {
            // A subnode was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node.
        // Move upwards until we reach a node of which we have not yet visited the last child.
        while (pindex)
        {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the corresponding variable.
            if (pindex == pindexFirstInvalid)
                pindexFirstInvalid = nullptr;
            if (pindex == pindexFirstMissing)
                pindexFirstMissing = nullptr;
            if (pindex == pindexFirstNeverProcessed)
                pindexFirstNeverProcessed = nullptr;
            if (pindex == pindexFirstNotTreeValid)
                pindexFirstNotTreeValid = nullptr;
            if (pindex == pindexFirstNotTransactionsValid)
                pindexFirstNotTransactionsValid = nullptr;
            if (pindex == pindexFirstNotChainValid)
                pindexFirstNotChainValid = nullptr;
            if (pindex == pindexFirstNotScriptsValid)
                pindexFirstNotScriptsValid = nullptr;
            // Find our parent.
            CBlockIndex *pindexPar = pindex->pprev;
            // Find which child we just visited.
            std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator, std::multimap<CBlockIndex *, CBlockIndex *>::iterator> rangePar = forward.equal_range(
                    pindexPar);
            while (rangePar.first->second != pindex)
            {
                assert(rangePar.first !=
                       rangePar.second); // Our parent must have at least the node we're coming from as child.
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second)
            {
                // Move to the sibling.
                pindex = rangePar.first->second;
                break;
            } else
            {
                // Move up further.
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}