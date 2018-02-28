///////////////////////////////////////////////////////////
//  CBlockIndexManager.cpp
//  Implementation of the Class CBlockIndexManager
//  Created on:      30-1-2018 16:40:57
//  Original author: marco
///////////////////////////////////////////////////////////

#include "blockindexmanager.h"
#include "blockfilemanager.h"
#include "utils.h"
#include "framework/warnings.h"
#include "chain.h"
#include "utils/timedata.h"

CBlockIndexManager::CBlockIndexManager()
{

}

CBlockIndexManager::~CBlockIndexManager()
{
    // block headers
    BlockMap::iterator it1 = mBlockIndex.begin();
    for (; it1 != mBlockIndex.end(); it1++)
        delete (*it1).second;
    mBlockIndex.clear();
}

int CBlockIndexManager::GetLastBlockFile()
{
    return iLastBlockFile;
}

std::vector<CBlockFileInfo> CBlockIndexManager::GetBlockFileInfo()
{
    return vecBlockFileInfo;
}

CBlockIndex *CBlockIndexManager::FindForkInGlobalIndex(const CChain &chain, const CBlockLocator &locator)
{
    // Find the first block the caller has in the main chain
    for (const uint256 &hash : locator.vHave)
    {
        BlockMap::iterator mi = mBlockIndex.find(hash);
        if (mi != mBlockIndex.end())
        {
            CBlockIndex *pindex = (*mi).second;
            if (chain.Contains(pindex))
                return pindex;
            if (pindex->GetAncestor(chain.Height()) == chain.Tip())
            {
                return chain.Tip();
            }
        }
    }
    return chain.Genesis();
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

bool CBlockIndexManager::Init(int64_t iBlockTreeDBCache, bool bReIndex, const CChainParams &chainparams)
{
    std::cout << "initialize index manager \n";

    UnLoadBlockIndex();

    pBlcokTreee = std::unique_ptr<CBlockTreeDB>(new CBlockTreeDB(iBlockTreeDBCache, false, bReIndex));

    if (bReIndex)
    {
        pBlcokTreee->WriteReindexing(true);
    }

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

int CBlockIndexManager::LoadBlockIndex(int64_t iBlockTreeDBCache, bool bReset, const CChainParams &chainparams)
{
    Init(iBlockTreeDBCache, bReset, chainparams);

    if (!bReIndex)
    {
        bool ret = LoadBlockIndexDB(chainparams);
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


    return Check(chainparams);
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

int CBlockIndexManager::Check(const CChainParams &chainparams)
{
    // If the loaded chain has a wrong genesis, bail out immediately
    // (we're likely using a testnet datadir, or the other way around).
    if (!mBlockIndex.empty() && (mBlockIndex.count(chainparams.GetConsensus().hashGenesisBlock) == 0))
    {
        return ERR_LOAD_GENESIS;
    }

    const CArgsManager &cArgs = app().GetArgsManager();
    // Check for changed -txindex state
    if (bTxIndex != cArgs.GetArg<bool>("-txindex", DEFAULT_TXINDEX))
    {
        return ERR_TXINDEX_STATE;
    }

    // Check for changed -prune state.  What we are concerned about is a user who has pruned blocks
    // in the past, but is now trying to run unpruned.
    if (bHavePruned && !fPruneMode)
    {
        return ERR_PRUNE_STATE;
    }

    return 0;
}

bool CBlockIndexManager::NeedInitGenesisBlock(const CChainParams &chainparams)
{
    LOCK(cs);

    // Check whether we're already initialized by checking for genesis in
    // mapBlockIndex. Note that we can't use chainActive here, since it is
    // set based on the coins db, not the block index db, which is the only
    // thing loaded at this point.
    if (bReIndex || mBlockIndex.count(chainparams.GenesisBlock().GetHash()) == 0)
        return true;

    return false;
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
    for (const auto &item : mBlockIndex)
    {
        CBlockIndex *pIndex = item.second;
        if (pIndex->nStatus & BLOCK_HAVE_DATA)
        {
            setBlkDataFiles.insert(pIndex->nFile);
        }
    }

    for (auto it = setBlkDataFiles.begin(); it != setBlkDataFiles.end(); it++)
    {
        CDiskBlockPos pos(*it, 0);
        if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull())
        {
            return false;
        }
    }

    return true;
}

bool CBlockIndexManager::LoadBlockIndexDB(const CChainParams &chainparams)
{
    const Consensus::Params &consensus = chainparams.GetConsensus();
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
        mlog.info("LoadBlockIndexDB(): Block files have previously been pruned\n");
    }

    bool bReIndexing = false;
    pBlcokTreee->ReadReindexing(bReIndexing);
    bReIndex |= bReIndexing;

    pBlcokTreee->ReadFlag("txindex", bTxIndex);
    mlog.info("transaction index %s\n", bTxIndex ? "enabled" : "disabled");

    return true;
}

void CBlockIndexManager::PruneBlockIndexCandidates()
{
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    std::set<CBlockIndex *, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, cChainActive.Tip()))
    {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
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
        mlog.error("LastCommonAncestor(): reorganization to unknown block requested");
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

    mlog.notice("%s: invalid block=%s  height=%d  log2_work=%.8g  date=%s", __func__,
                pIndexNew->GetBlockHash().ToString(), pIndexNew->nHeight,
                log(pIndexNew->nChainWork.getdouble()) / log(2.0), DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
                                                                                     pIndexNew->GetBlockTime()));
    CBlockIndex *tip = cChainActive.Tip();
    assert (tip);
    mlog.notice("%s:  current best=%s  height=%d  log2_work=%.8g  date=%s", __func__,
                tip->GetBlockHash().ToString(), cChainActive.Height(), log(tip->nChainWork.getdouble()) / log(2.0),
                DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime()));
    //    CheckForkWarningConditions();
}

void CBlockIndexManager::InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state)
{
    if (!state.CorruptionPossible())
    {
        pindex->nStatus |= BLOCK_FAILED_VALID;
        setFailedBlocks.insert(pindex);
        setDirtyBlockIndex.insert(pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex);
    }
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
    for (auto it = mBlockIndex.begin(); it != mBlockIndex.end(); it++)
    {
        forward.insert(std::make_pair(it->second->pprev, it->second));
    }

    assert(forward.size() == mBlockIndex.size());

    //    std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator, std::multimap<CBlockIndex *, CBlockIndex *>::iterator> rangeGenesis = forward.equal_range(
    //            nullptr);
    auto rangeGenesis = forward.equal_range(nullptr);

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

void CBlockIndexManager::InvalidateBlock(CBlockIndex *pIndex, CBlockIndex *pInvalidWalkTip, bool bIndexWasInChain)
{
    while (bIndexWasInChain && pInvalidWalkTip != pIndex)
    {
        pInvalidWalkTip->nStatus |= BLOCK_FAILED_CHILD;
        setDirtyBlockIndex.insert(pInvalidWalkTip);
        setBlockIndexCandidates.erase(pInvalidWalkTip);
        pInvalidWalkTip = pInvalidWalkTip->pprev;
    }

    // Mark the block itself as invalid.
    pIndex->nStatus |= BLOCK_FAILED_VALID;
    setDirtyBlockIndex.insert(pIndex);
    setBlockIndexCandidates.erase(pIndex);
    setFailedBlocks.insert(pIndex);

    // The resulting new best tip may not be in setBlockIndexCandidates anymore, so
    // add it again.
    BlockMap::iterator it = mBlockIndex.begin();
    while (it != mBlockIndex.end())
    {
        if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx &&
            !setBlockIndexCandidates.value_comp()(it->second, cChainActive.Tip()))
        {
            setBlockIndexCandidates.insert(it->second);
        }
        it++;
    }
}

void CBlockIndexManager::CheckForkWarningConditions()
{
    // If our best fork is no longer within 72 blocks (+/- 12 hours if no one mines it)
    // of our head, drop it
    if (pIndexBestForkTip && cChainActive.Height() - pIndexBestForkTip->nHeight >= 72)
        pIndexBestForkTip = nullptr;

    if (pIndexBestForkTip || (pIndexBestInvalid && pIndexBestInvalid->nChainWork > cChainActive.Tip()->nChainWork +
                                                                                   (GetBlockProof(*cChainActive.Tip()) *
                                                                                    6)))
    {
        if (!GetfLargeWorkForkFound() && pIndexBestForkBase)
        {
            std::string warning = std::string("'Warning: Large-work fork detected, forking after block ") +
                                  pIndexBestForkBase->phashBlock->ToString() + std::string("'");
            AlertNotify(warning);
        }
        if (pIndexBestForkTip && pIndexBestForkBase)
        {
            mlog.warn(
                    "%s: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s).\nChain state database corruption likely.\n",
                    __func__,
                    pIndexBestForkBase->nHeight, pIndexBestForkBase->phashBlock->ToString(),
                    pIndexBestForkTip->nHeight, pIndexBestForkTip->phashBlock->ToString());
            SetfLargeWorkForkFound(true);
        } else
        {
            mlog.warn(
                    "%s: Warning: Found invalid chain at least ~6 blocks longer than our best chain.\nChain state database corruption likely.\n",
                    __func__);
            SetfLargeWorkInvalidChainFound(true);
        }
    } else
    {
        SetfLargeWorkForkFound(false);
        SetfLargeWorkInvalidChainFound(false);
    }
}

void CBlockIndexManager::CheckForkWarningConditionsOnNewFork(CBlockIndex *pindexNewForkTip)
{
    AssertLockHeld(cs);
    // If we are on a fork that is sufficiently large, set a warning flag
    CBlockIndex *pfork = pindexNewForkTip;
    CBlockIndex *plonger = cChainActive.Tip();
    while (pfork && pfork != plonger)
    {
        while (plonger && plonger->nHeight > pfork->nHeight)
            plonger = plonger->pprev;
        if (pfork == plonger)
            break;
        pfork = pfork->pprev;
    }

    // We define a condition where we should warn the user about as a fork of at least 7 blocks
    // with a tip within 72 blocks (+/- 12 hours if no one mines it) of ours
    // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
    // hash rate operating on the fork.
    // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
    // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
    // the 7-block condition and from this always have the most-likely-to-cause-warning fork
    if (pfork && (!pIndexBestForkTip || pindexNewForkTip->nHeight > pIndexBestForkTip->nHeight) &&
        pindexNewForkTip->nChainWork - pfork->nChainWork > (GetBlockProof(*pfork) * 7) &&
        cChainActive.Height() - pindexNewForkTip->nHeight < 72)
    {
        pIndexBestForkTip = pindexNewForkTip;
        pIndexBestForkBase = pfork;
    }

    CheckForkWarningConditions();
}

bool CBlockIndexManager::IsWitnessEnabled(const CBlockIndex *pIndexPrev, const Consensus::Params &params)
{
    LOCK(cs);
    return (VersionBitsState(pIndexPrev, params, Consensus::DEPLOYMENT_SEGWIT, versionbitscache) == THRESHOLD_ACTIVE);
}

bool CBlockIndexManager::NeedRewind(const int height, const Consensus::Params &params)
{
    assert(height >= 1);

    if (IsWitnessEnabled(cChainActive[height - 1], params) && !(cChainActive[height]->nStatus & BLOCK_OPT_WITNESS))
    {
        return true;
    }

    return false;
}

void CBlockIndexManager::RewindBlockIndex(const Consensus::Params &params)
{
    for (auto it = mBlockIndex.begin(); it != mBlockIndex.end(); it++)
    {
        CBlockIndex *pIndexIter = it->second;

        // Note: If we encounter an insufficiently validated block that
        // is on chainActive, it must be because we are a pruning node, and
        // this block or some successor doesn't HAVE_DATA, so we were unable to
        // rewind all the way.  Blocks remaining on chainActive at this point
        // must not have their validity reduced.
        if (IsWitnessEnabled(pIndexIter->pprev, params) && !(pIndexIter->nStatus & BLOCK_OPT_WITNESS) &&
            !cChainActive.Contains(pIndexIter))
        {
            // Reduce validity
            pIndexIter->nStatus = std::min<unsigned int>(pIndexIter->nStatus & BLOCK_VALID_MASK, BLOCK_VALID_TREE) |
                                  (pIndexIter->nStatus & ~BLOCK_VALID_MASK);
            // Remove have-data flags.
            pIndexIter->nStatus &= ~(BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO);
            // Remove storage location.
            pIndexIter->nFile = 0;
            pIndexIter->nDataPos = 0;
            pIndexIter->nUndoPos = 0;
            // Remove various other things
            pIndexIter->nTx = 0;
            pIndexIter->nChainTx = 0;
            pIndexIter->nSequenceId = 0;
            // Make sure it gets written.
            setDirtyBlockIndex.insert(pIndexIter);
            // Update indexes
            setBlockIndexCandidates.erase(pIndexIter);
            std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator, std::multimap<CBlockIndex *, CBlockIndex *>::iterator> ret = mBlocksUnlinked.equal_range(
                    pIndexIter->pprev);
            while (ret.first != ret.second)
            {
                if (ret.first->second == pIndexIter)
                {
                    mBlocksUnlinked.erase(ret.first++);
                } else
                {
                    ++ret.first;
                }
            }
        } else if (pIndexIter->IsValid(BLOCK_VALID_TRANSACTIONS) && pIndexIter->nChainTx)
        {
            setBlockIndexCandidates.insert(pIndexIter);
        }
    }

    if (cChainActive.Tip() != nullptr)
    {
        // We can't prune block index candidates based on our tip if we have
        // no tip due to chainActive being empty!
        PruneBlockIndexCandidates();

        CheckBlockIndex(params);
    }
}

bool CBlockIndexManager::IsOnlyGenesisBlockIndex()
{
    return (mBlockIndex.size() == 1);
}

bool CBlockIndexManager::AcceptBlockHeader(const CBlockHeader &block, CValidationState &state,
                                           const CChainParams &chainparams, CBlockIndex **ppindex)
{
    AssertLockHeld(cs);
    // Check for duplicate
    uint256 hash = block.GetHash();
    auto miSelf = mBlockIndex.find(hash);
    CBlockIndex *pindex = nullptr;
    if (hash != chainparams.GetConsensus().hashGenesisBlock)
    {

        if (miSelf != mBlockIndex.end())
        {
            // Block header is already known.
            pindex = miSelf->second;
            if (ppindex)
                *ppindex = pindex;
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return state.Invalid(error("%s: block %s is marked invalid", __func__, hash.ToString()), 0,
                                     "duplicate");
            return true;
        }

        if (!CheckBlockHeader(block, state, chainparams.GetConsensus()))
            return error("%s: Consensus::CheckBlockHeader: %s, %s", __func__, hash.ToString(),
                         FormatStateMessage(state));

        // Get prev block index
        CBlockIndex *pindexPrev = nullptr;
        BlockMap::iterator mi = mBlockIndex.find(block.hashPrevBlock);
        if (mi == mBlockIndex.end())
            return state.DoS(10, error("%s: prev block not found", __func__), 0, "prev-blk-not-found");
        pindexPrev = (*mi).second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK)
            return state.DoS(100, error("%s: prev block invalid", __func__), REJECT_INVALID, "bad-prevblk");
        if (!ContextualCheckBlockHeader(block, state, chainparams, pindexPrev, GetAdjustedTime()))
            return error("%s: Consensus::ContextualCheckBlockHeader: %s, %s", __func__, hash.ToString(),
                         FormatStateMessage(state));

        if (!pindexPrev->IsValid(BLOCK_VALID_SCRIPTS))
        {
            for (const CBlockIndex *failedit : setFailedBlocks)
            {
                if (pindexPrev->GetAncestor(failedit->nHeight) == failedit)
                {
                    assert(failedit->nStatus & BLOCK_FAILED_VALID);
                    CBlockIndex *invalid_walk = pindexPrev;
                    while (invalid_walk != failedit)
                    {
                        invalid_walk->nStatus |= BLOCK_FAILED_CHILD;
                        setDirtyBlockIndex.insert(invalid_walk);
                        invalid_walk = invalid_walk->pprev;
                    }
                    return state.DoS(100, error("%s: prev block invalid", __func__), REJECT_INVALID, "bad-prevblk");
                }
            }
        }
    }
    if (pindex == nullptr)
        pindex = AddToBlockIndex(block);

    if (ppindex)
        *ppindex = pindex;

    CheckBlockIndex(chainparams.GetConsensus());

    return true;
}

bool CBlockIndexManager::CheckBlockHeader(const CBlockHeader &block, CValidationState &state,
                                          const Consensus::Params &consensusParams, bool fCheckPOW)
{
    // Check proof of work matches claimed amount
    if (fCheckPOW && !CheckProofOfWork(block.GetHash(), block.nBits, consensusParams))
        return state.DoS(50, false, REJECT_INVALID, "high-hash", false, "proof of work failed");

    return true;
}

log4cpp::Category &CBlockIndexManager::mlog = log4cpp::Category::getInstance(EMTOSTR(CID_BLOCK_CHAIN));

/** Context-dependent validity checks.
 *  By "context", we mean only the previous block headers, but not the UTXO
 *  set; UTXO-related validity checks are done in ConnectBlock(). */
bool CBlockIndexManager::ContextualCheckBlockHeader(const CBlockHeader &block, CValidationState &state,
                                                    const CChainParams &params, const CBlockIndex *pindexPrev,
                                                    int64_t nAdjustedTime)
{
    assert(pindexPrev != nullptr);
    const int nHeight = pindexPrev->nHeight + 1;

    // Check proof of work
    const Consensus::Params &consensusParams = params.GetConsensus();
    if (block.nBits != GetNextWorkRequired(pindexPrev, &block, consensusParams))
        return state.DoS(100, false, REJECT_INVALID, "bad-diffbits", false, "incorrect proof of work");

    // Check against checkpoints
    if (fCheckpointsEnabled)
    {
        // Don't accept any forks from the main chain prior to last checkpoint.
        // GetLastCheckpoint finds the last checkpoint in MapCheckpoints that's in our
        // MapBlockIndex.
        if (IsAgainstCheckPoint(params, pindexPrev) || IsAgainstCheckPoint(params, nHeight, block.GetHash()))
            return state.DoS(100, error("%s: forked chain older than last checkpoint (height %d)", __func__, nHeight),
                             REJECT_CHECKPOINT, "bad-fork-prior-to-checkpoint");
    }

    // Check timestamp against prev
    if (block.GetBlockTime() <= pindexPrev->GetMedianTimePast())
        return state.Invalid(false, REJECT_INVALID, "time-too-old", "block's timestamp is too early");

    // Check timestamp
    if (block.GetBlockTime() > nAdjustedTime + MAX_FUTURE_BLOCK_TIME)
        return state.Invalid(false, REJECT_INVALID, "time-too-new", "block timestamp too far in the future");

    // Reject outdated version blocks when 95% (75% on testnet) of the network has upgraded:
    // check for version 2, 3 and 4 upgrades
    if ((block.nVersion < 2 && nHeight >= consensusParams.BIP34Height) ||
        (block.nVersion < 3 && nHeight >= consensusParams.BIP66Height) ||
        (block.nVersion < 4 && nHeight >= consensusParams.BIP65Height))
        return state.Invalid(false, REJECT_OBSOLETE, strprintf("bad-version(0x%08x)", block.nVersion),
                             strprintf("rejected nVersion=0x%08x block", block.nVersion));

    return true;
}

CBlockIndex *CBlockIndexManager::GetIndexBestHeader()
{
    return pIndexBestHeader;
}

bool CBlockIndexManager::SetDirtyIndex(CBlockIndex *pIndex)
{
    setDirtyBlockIndex.insert(pIndex);

    return true;
}

bool CBlockIndexManager::FindBlockPos(CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize,
                                      unsigned int nHeight, uint64_t nTime, bool fKnown)
{
    LOCK(csLastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : iLastBlockFile;
    if (vecBlockFileInfo.size() <= nFile)
    {
        vecBlockFileInfo.resize(nFile + 1);
    }

    if (!fKnown)
    {
        while (vecBlockFileInfo[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE)
        {
            nFile++;
            if (vecBlockFileInfo.size() <= nFile)
            {
                vecBlockFileInfo.resize(nFile + 1);
            }
        }
        pos.nFile = nFile;
        pos.nPos = vecBlockFileInfo[nFile].nSize;
    }

    if ((int)nFile != iLastBlockFile)
    {
        if (!fKnown)
        {
            mlog.info("Leaving block file %i: %s\n", iLastBlockFile, vecBlockFileInfo[iLastBlockFile].ToString());
        }
        FlushBlockFile(!fKnown, vecBlockFileInfo[iLastBlockFile].nSize, vecBlockFileInfo[iLastBlockFile].nUndoSize);
        iLastBlockFile = nFile;
    }

    vecBlockFileInfo[nFile].AddBlock(nHeight, nTime);
    if (fKnown)
        vecBlockFileInfo[nFile].nSize = std::max(pos.nPos + nAddSize, vecBlockFileInfo[nFile].nSize);
    else
        vecBlockFileInfo[nFile].nSize += nAddSize;

    if (!fKnown)
    {
        unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        unsigned int nNewChunks = (vecBlockFileInfo[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks)
        {
            //            if (fPruneMode) todo
            //                fCheckForPruning = true;
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos))
            {
                FILE *file = OpenBlockFile(pos);
                if (file)
                {
                    mlog.info("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE,
                              pos.nFile);
                    AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                    fclose(file);
                }
            } else
                return state.Error("out of disk space");
        }
    }

    setDirtyFileInfo.insert(nFile);
    return true;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool CBlockIndexManager::ReceivedBlockTransactions(const CBlock &block, CValidationState &state, CBlockIndex *pindexNew,
                                                   const CDiskBlockPos &pos, const Consensus::Params &consensusParams)
{
    pindexNew->nTx = block.vtx.size();
    pindexNew->nChainTx = 0;
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;
    if (IsWitnessEnabled(pindexNew->pprev, consensusParams))
    {
        pindexNew->nStatus |= BLOCK_OPT_WITNESS;
    }
    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    setDirtyBlockIndex.insert(pindexNew);

    if (pindexNew->pprev == nullptr || pindexNew->pprev->nChainTx)
    {
        // If pindexNew is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
        std::deque<CBlockIndex *> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to be connected.
        while (!queue.empty())
        {
            CBlockIndex *pindex = queue.front();
            queue.pop_front();
            pindex->nChainTx = (pindex->pprev ? pindex->pprev->nChainTx : 0) + pindex->nTx;
            {
                //                LOCK(cs_nBlockSequenceId); todo
                //                pindex->nSequenceId = nBlockSequenceId++;
            }
            if (cChainActive.Tip() == nullptr || !setBlockIndexCandidates.value_comp()(pindex, cChainActive.Tip()))
            {
                setBlockIndexCandidates.insert(pindex);
            }
            std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator, std::multimap<CBlockIndex *, CBlockIndex *>::iterator> range = mBlocksUnlinked.equal_range(
                    pindex);
            while (range.first != range.second)
            {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator it = range.first;
                queue.push_back(it->second);
                range.first++;
                mBlocksUnlinked.erase(it);
            }
        }
    } else
    {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE))
        {
            mBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }

    return true;
}

bool CBlockIndexManager::PreciousBlock(CValidationState &state, const CChainParams &params, CBlockIndex *pindex)
{
    {
        LOCK(cs);
        if (pindex->nChainWork < cChainActive.Tip()->nChainWork)
        {
            // Nothing to do, this block is not at the tip.
            return true;
        }
        if (cChainActive.Tip()->nChainWork > nLastPreciousChainwork)
        {
            // The chain has been extended since the last call, reset the counter.
            nBlockReverseSequenceId = -1;
        }
        nLastPreciousChainwork = cChainActive.Tip()->nChainWork;
        setBlockIndexCandidates.erase(pindex);
        pindex->nSequenceId = nBlockReverseSequenceId;
        if (nBlockReverseSequenceId > std::numeric_limits<int32_t>::min())
        {
            // We can't keep reducing the counter if somebody really wants to
            // call preciousblock 2**31-1 times on the same set of tips...
            nBlockReverseSequenceId--;
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && pindex->nChainTx)
        {
            setBlockIndexCandidates.insert(pindex);
            PruneBlockIndexCandidates();
        }
    }
}

bool CBlockIndexManager::ResetBlockFailureFlags(CBlockIndex *pindex)
{
    AssertLockHeld(cs);

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    BlockMap::iterator it = mBlockIndex.begin();
    while (it != mBlockIndex.end())
    {
        if (!it->second->IsValid() && it->second->GetAncestor(nHeight) == pindex)
        {
            it->second->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(it->second);
            if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx &&
                setBlockIndexCandidates.value_comp()(cChainActive.Tip(), it->second))
            {
                setBlockIndexCandidates.insert(it->second);
            }
            if (it->second == pIndexBestInvalid)
            {
                // Reset invalid block marker if it was pointing to one of those.
                pIndexBestInvalid = nullptr;
            }
            setFailedBlocks.erase(it->second);
        }
        it++;
    }

    // Remove the invalidity flag from all ancestors too.
    while (pindex != nullptr)
    {
        if (pindex->nStatus & BLOCK_FAILED_MASK)
        {
            pindex->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(pindex);
        }
        pindex = pindex->pprev;
    }
    return true;
}

int64_t CBlockIndexManager::GetBlockProofEquivalentTime(uint256 hashAssumeValid, const CBlockIndex *pindex,
                                                        const CChainParams &params)
{
    // We've been configured with the hash of a block which has been externally verified to have a valid history.
    // A suitable default value is included with the software and updated from time to time.  Because validity
    //  relative to a piece of software is an objective fact these defaults can be easily reviewed.
    // This setting doesn't force the selection of any particular chain but makes validating some faster by
    //  effectively caching the result of part of the verification.
    BlockMap::const_iterator it = mBlockIndex.find(hashAssumeValid);
    if (it != mBlockIndex.end())
    {
        if (it->second->GetAncestor(pindex->nHeight) == pindex &&
            pIndexBestHeader->GetAncestor(pindex->nHeight) == pindex &&
            pIndexBestHeader->nChainWork >= nMinimumChainWork)
        {
            // This block is a member of the assumed verified chain and an ancestor of the best header.
            // The equivalent time check discourages hash power from extorting the network via DOS attack
            //  into accepting an invalid block through telling users they must manually set assumevalid.
            //  Requiring a software change or burying the invalid block, regardless of the setting, makes
            //  it hard to hide the implication of the demand.  This also avoids having release candidates
            //  that are hardly doing any signature verification at all in testing without having to
            //  artificially set the default assumed verified block further back.
            // The test against nMinimumChainWork prevents the skipping when denied access to any chain at
            //  least as good as the expected chain.
            ::GetBlockProofEquivalentTime(*pIndexBestHeader, *pindex, *pIndexBestHeader, params.GetConsensus());
        }
    }
}

std::set<const CBlockIndex *, CompareBlocksByHeight> CBlockIndexManager::GetTips()
{
    std::set<const CBlockIndex *, CompareBlocksByHeight> setTips;
    std::set<const CBlockIndex *> setOrphans;
    std::set<const CBlockIndex *> setPrevs;

    for (const std::pair<const uint256, CBlockIndex *> &item : mBlockIndex)
    {
        if (!cChainActive.Contains(item.second))
        {
            setOrphans.insert(item.second);
            setPrevs.insert(item.second->pprev);
        }
    }

    for (std::set<const CBlockIndex *>::iterator it = setOrphans.begin(); it != setOrphans.end(); ++it)
    {
        if (setPrevs.erase(*it) == 0)
        {
            setTips.insert(*it);
        }
    }

    // Always report the currently active tip.
    setTips.insert(cChainActive.Tip());
}

CBlockTreeDB *CBlockIndexManager::GetBlockTreeDB()
{
    return pBlcokTreee.get();
}