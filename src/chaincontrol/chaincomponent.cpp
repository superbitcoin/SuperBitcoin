#include <iostream>
#include <thread>
#include "chaincomponent.h"
#include "checkpoints.h"
#include "sbtccore/streams.h"
#include "interface/inetcomponent.h"
#include "utils/net/netmessagehelper.h"
#include "sbtccore/block/validation.h"
#include "sbtccore/transaction/script/sigcache.h"
#include "sbtccore/transaction/policy.h"
#include "utils/reverse_iterator.h"
#include "interface/imempoolcomponent.h"
#include "framework/warnings.h"
#include "utils/util.h"
#include "utils/timedata.h"
#include "sbtccore/block/merkle.h"
#include "sbtccore/checkqueue.h"
#include "sbtccore/block/undo.h"
#include "utils/merkleblock.h"
#include "framework/base.hpp"
#include "framework/validationinterface.h"
#include "eventmanager/eventmanager.h"
#include "utils.h"

static int64_t nTimeCheck = 0;
static int64_t nTimeForks = 0;
static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;
static int64_t nTimeTotal = 0;

log4cpp::Category &CChainComponent::mlog = log4cpp::Category::getInstance(EMTOSTR(CID_BLOCK_CHAIN));

void CChainComponent::ThreadScriptCheck()
{
    RenameThread("bitcoin-scriptch");
    scriptCheckQueue.Thread();
}

CChainComponent::CChainComponent()
        : scriptCheckQueue(128)
{
}

CChainComponent::~CChainComponent()
{
}

bool CChainComponent::ComponentInitialize()
{
    std::cout << "initialize chain component \n";

    app().GetEventManager().RegisterEventHandler(EID_NODE_DISCONNECTED, this, &CChainComponent::OnNodeDisconnected);

    LoadCheckPoint();

    InitSignatureCache((int64_t)(Args().GetArg<uint32_t>("-maxsigcachesize", DEFAULT_MAX_SIG_CACHE_SIZE) / 2));

    InitScriptExecutionCache(int64_t(Args().GetArg<uint32_t>("-maxsigcachesize", DEFAULT_MAX_SIG_CACHE_SIZE) / 2));

    // -par=0 means autodetect, but nScriptCheckThreads==0 means no concurrency
    nScriptCheckThreads = Args().GetArg<int32_t>("-par", DEFAULT_SCRIPTCHECK_THREADS);
    if (nScriptCheckThreads <= 0)
        nScriptCheckThreads += GetNumCores();
    if (nScriptCheckThreads <= 1)
        nScriptCheckThreads = 0;
    else if (nScriptCheckThreads > MAX_SCRIPTCHECK_THREADS)
        nScriptCheckThreads = MAX_SCRIPTCHECK_THREADS;
    mlog_notice("Using %u threads for script verification.", nScriptCheckThreads);
    if (nScriptCheckThreads)
    {
        for (int i = 0; i < nScriptCheckThreads; i++)
            threadGroup.create_thread(boost::bind(&CChainComponent::ThreadScriptCheck, this));
    }

    // block pruning; get the amount of disk space (in MiB) to allot for block & undo files
    int64_t iPruneArg = Args().GetArg<int32_t>("-prune", 0);
    if (iPruneArg < 0)
    {
        return InitError(_("Prune cannot be configured with a negative value."));
    }
    nPruneTarget = (uint64_t)iPruneArg * 1024 * 1024;
    if (iPruneArg == 1)
    {  // manual pruning: -prune=1
        mlog_error(
                "Block pruning enabled.  Use RPC call pruneblockchain(height) to manually prune block and undo files.\n");
        nPruneTarget = std::numeric_limits<uint64_t>::max();
        fPruneMode = true;
    } else if (nPruneTarget)
    {
        if (nPruneTarget < MIN_DISK_SPACE_FOR_BLOCK_FILES)
        {
            return InitError(strprintf(_("Prune configured below the minimum of %d MiB.  Please use a higher number."),
                                       MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024));
        }
        mlog_error("Prune configured to target %uMiB on disk for block and undo files.\n", nPruneTarget / 1024 / 1024);
        fPruneMode = true;
    }

    bool bReIndex = Args().GetArg<bool>("-reindex", false);
    bool bReindexChainState = Args().GetArg<bool>("-reindex-chainstate", false);
    bool bTxIndex = Args().GetArg<bool>("-txindex", DEFAULT_TXINDEX);

    // cache size calculations
    int64_t iTotalCache = (Args().GetArg<int64_t>("-dbcache", nDefaultDbCache) << 20);
    iTotalCache = std::max(iTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    iTotalCache = std::min(iTotalCache, nMaxDbCache << 20); // total cache cannot be greater than nMaxDbcache
    int64_t iBlockTreeDBCache = iTotalCache / 8;
    iBlockTreeDBCache = std::min(iBlockTreeDBCache,
                                 (Args().GetArg<bool>("-txindex", DEFAULT_TXINDEX) ? nMaxBlockDBAndTxIndexCache
                                                                                   : nMaxBlockDBCache) << 20);
    iTotalCache -= iBlockTreeDBCache;
    int64_t iCoinDBCache = std::min(iTotalCache / 2,
                                    (iTotalCache / 4) + (1 << 23)); // use 25%-50% of the remainder for disk cache
    iCoinDBCache = std::min(iCoinDBCache, nMaxCoinsDBCache << 20); // cap total coins db cache
    iTotalCache -= iCoinDBCache;
    iCoinCacheUsage = iTotalCache; // the rest goes to in-memory cache
    mlog_error("Cache configuration:\n");
    mlog_error("* Using %.1fMiB for block index database\n", iBlockTreeDBCache * (1.0 / 1024 / 1024));
    mlog_error("* Using %.1fMiB for chain state database\n", iCoinDBCache * (1.0 / 1024 / 1024));
    mlog_error("* Using %.1fMiB for in-memory UTXO set \n", iCoinCacheUsage * (1.0 / 1024 / 1024));

    int64_t iStart;
    bool bLoaded = false;
    while (!bLoaded && !bRequestShutdown)
    {
        bool bReset = bReIndex;
        std::string strLoadError;

        uiInterface.InitMessage(_("Loading block index..."));

        iStart = GetTimeMillis();

        do
        {
            Init();

            if (bReset && fPruneMode)
            {
                CleanupBlockRevFiles();
            }

            if (bRequestShutdown)
            {
                break;
            }

            int ret = cIndexManager.LoadBlockIndex(Params().GetConsensus(), iBlockTreeDBCache, bReset, bTxIndex);
            if (ret == ERR_LOAD_INDEX_DB)
            {
                strLoadError = _("Error loading block database");
                break;
            } else if (ret == ERR_LOAD_GENESIS)
            {
                return InitError(_("Incorrect or no genesis block found. Wrong datadir for network?"));
            } else if (ret == ERR_TXINDEX_STATE)
            {
                strLoadError = _("You need to rebuild the database using -reindex to change -txindex");
                break;
            } else if (ret == ERR_PRUNE_STATE)
            {
                strLoadError = _(
                        "You need to rebuild the database using -reindex to go back to unpruned mode.  This will redownload the entire blockchain");
                break;
            }

            bReIndex |= cIndexManager.IsReIndexing();
            if (!bReIndex && cIndexManager.NeedInitGenesisBlock(Params()))
            {
                if (!LoadGenesisBlock(Params()))
                {
                    strLoadError = _("Error initializing block database");
                    break;
                }
            }

            uiInterface.InitMessage(_("Init View..."));
            ret = cViewManager.InitCoinsDB(iCoinDBCache, bReset | bReindexChainState);
            if (ret == ERR_VIEW_UPGRADE)
            {
                strLoadError = _("Error upgrading chainstate database");
                break;
            }

            // ReplayBlocks is a no-op if we cleared the coinsviewdb with -reindex or -reindex-chainstate
            if (!ReplayBlocks())
            {
                strLoadError = _(
                        "Unable to replay blocks. You will need to rebuild the database using -reindex-chainstate.");
                break;
            }

            // The on-disk coinsdb is now in a good state, create the cache
            cViewManager.InitCoinsCache();

            bool bCoinsViewEmpty =
                    bReset || bReindexChainState || cViewManager.GetCoinsTip()->GetBestBlock().IsNull();
            if (!bCoinsViewEmpty)
            {
                // LoadChainTip sets chainActive based on pcoinsTip's best block
                if (!LoadChainTip(Params()))
                {
                    strLoadError = _("Error initializing block database");
                    break;
                }
                // check current chain according to checkpoint
                CValidationState state;
                CheckActiveChain(state, Params());
                assert(state.IsValid());
                assert(Tip() != nullptr);
            }

            if (!bReset)
            {
                // Note that RewindBlockIndex MUST run even if we're about to -reindex-chainstate.
                // It both disconnects blocks based on chainActive, and drops block data in
                // mapBlockIndex based on lack of available witness data.
                uiInterface.InitMessage(_("Rewinding blocks..."));
                if (!RewindBlock(Params()))
                {
                    strLoadError = _(
                            "Unable to rewind the database to a pre-fork state. You will need to redownload the blockchain");
                    break;
                }
            }

            if (!bCoinsViewEmpty)
            {
                ret = VerifyBlocks();
                if (ret == OK_CHAIN)
                {

                } else if (ret == ERR_FUTURE_BLOCK)
                {
                    strLoadError = _("The block database contains a block which appears to be from the future. "
                                             "This may be due to your computer's date and time being set incorrectly. "
                                             "Only rebuild the block database if you are sure that your computer's date and time are correct");
                    break;
                } else if (ret == ERR_VERIFY_DB)
                {
                    strLoadError = _("Corrupted block database detected");
                    break;
                }
            }

            bLoaded = true;
        } while (false);

        if (!bLoaded && !bRequestShutdown)
        {
            // first suggest a reindex
            if (!bReset)
            {
                bool bRet = uiInterface.ThreadSafeQuestion(
                        strLoadError + ".\n\n" + _("Do you want to rebuild the block database now?"),
                        strLoadError + ".\nPlease restart with -reindex or -reindex-chainstate to recover.",
                        "", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
                if (bRet)
                {
                    bReIndex = true;
                    bRequestShutdown = false;
                } else
                {
                    mlog.fatal("Aborted block database rebuild. Exiting.\n");
                    return false;
                }
            } else
            {
                return InitError(strLoadError);
            }
        }
    }

    // As LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (bRequestShutdown)
    {
        mlog_error("Shutdown requested. Exiting.\n");
        return false;
    }
    if (bLoaded)
    {
        mlog_error(" block index %15dms\n", GetTimeMillis() - iStart);
    }

    // if pruning, unset the service bit and perform the initial blockstore prune
    // after any wallet rescanning has taken place.
    if (fPruneMode)
    {
        mlog_notice("Unsetting NODE_NETWORK on prune mode.");
        nLocalServices = ServiceFlags(nLocalServices & ~NODE_NETWORK);
        if (!bReIndex)
        {
            mlog_notice("Pruning blockstore...");
            //PruneAndFlush();
        }
    }

    if (Params().GetConsensus().vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout != 0)
    {
        // Only advertise witness capabilities if they have a reasonable start time.
        // This allows us to have the code merged without a defined softfork, by setting its
        // end time to 0.
        // Note that setting NODE_WITNESS is never required: the only downside from not
        // doing so is that after activation, no upgraded nodes will fetch from you.
        nLocalServices = ServiceFlags(nLocalServices | NODE_WITNESS);
        // Only care about others providing witness capabilities if there is a softfork
        // defined.
        nRelevantServices = ServiceFlags(nRelevantServices | NODE_WITNESS);
    }

    return true;
}

bool CChainComponent::ComponentStartup()
{
    std::cout << "startup chain component \n";
    bRequestShutdown = false;

    ThreadImport();
    //    std::thread t(&CChainComponent::ThreadImport, this);
    //    t.detach();
    return true;
}

bool CChainComponent::ComponentShutdown()
{
    std::cout << "shutdown chain component \n";

    threadGroup.interrupt_all();
    threadGroup.join_all();

    if (cViewManager.GetCoinsTip() != nullptr)
    {
        FlushStateToDisk();
    }

    bRequestShutdown = true;
    cViewManager.RequestShutdown();
    return true;
}

void CChainComponent::Init()
{
    versionbitscache.Clear();
    for (int b = 0; b < VERSIONBITS_NUM_BITS; b++)
    {
        warningcache[b].clear();
    }
}

bool CChainComponent::IsImporting() const
{
    return fImporting;
}

bool CChainComponent::IsReindexing() const
{
    return bReIndex;
}

bool CChainComponent::IsTxIndex() const
{
    return cIndexManager.IsTxIndex();
}

bool CChainComponent::DoesBlockExist(uint256 hash)
{
    return (cIndexManager.GetBlockIndex(hash) != nullptr);
}

CBlockIndex *CChainComponent::GetBlockIndex(uint256 hash)
{
    return cIndexManager.GetBlockIndex(hash);
}

bool CChainComponent::LoadGenesisBlock(const CChainParams &chainparams)
{
    LOCK(cs);

    try
    {
        CBlock &block = const_cast<CBlock &>(chainparams.GenesisBlock());
        // Start new block file
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        CValidationState state;
        if (!cIndexManager.FindBlockPos(state, blockPos, nBlockSize + 8, 0, block.GetBlockTime()))
            return error("%s: FindBlockPos failed", __func__);
        if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
            return error("%s: writing genesis block to disk failed", __func__);
        CBlockIndex *pindex = cIndexManager.AddToBlockIndex(block);
        if (!cIndexManager.ReceivedBlockTransactions(block, state, pindex, blockPos, chainparams.GetConsensus()))
            return error("%s: genesis block not accepted", __func__);
    } catch (const std::runtime_error &e)
    {
        return error("%s: failed to write genesis block: %s", __func__, e.what());
    }

    return true;
}

int CChainComponent::GetActiveChainHeight()
{
    return cIndexManager.GetChain().Height();
}

bool CChainComponent::GetActiveChainTipHash(uint256 &tipHash)
{
    if (CBlockIndex *tip = cIndexManager.GetChain().Tip())
    {
        tipHash = tip->GetBlockHash();
        return true;
    }

    return false;
}

CChain &CChainComponent::GetActiveChain()
{
    return cIndexManager.GetChain();
}

std::set<const CBlockIndex *, CompareBlocksByHeight> CChainComponent::GetTips()
{
    return cIndexManager.GetTips();
}

CBlockIndex *CChainComponent::Tip()
{
    return cIndexManager.GetChain().Tip();
}

void CChainComponent::SetTip(CBlockIndex *pIndexTip)
{
    cIndexManager.GetChain().SetTip(pIndexTip);
}

bool CChainComponent::ReplayBlocks()
{
    CCoinsView *view(cViewManager.GetCoinViewDB());
    CCoinsViewCache cache(view);

    std::vector<uint256> vecHashHeads = view->GetHeadBlocks();
    if (vecHashHeads.empty())
        return true; // We're already in a consistent state.
    if (vecHashHeads.size() != 2)
        return error("ReplayBlocks(): unknown inconsistent state");

    mlog.info("Replaying blocks\n");

    const CBlockIndex *pIndexOld = nullptr;  // Old tip during the interrupted flush.
    const CBlockIndex *pIndexNew;            // New tip during the interrupted flush.
    const CBlockIndex *pIndexFork = nullptr; // Latest block common to both the old and the new tip.

    pIndexNew = cIndexManager.GetBlockIndex(vecHashHeads[0]);
    if (!pIndexNew)
    {
        return false;
    }

    if (!vecHashHeads[1].IsNull())
    {
        pIndexOld = cIndexManager.GetBlockIndex(vecHashHeads[1]);
        if (!pIndexOld)
        {
            return false;
        }
        pIndexFork = LastCommonAncestor(pIndexNew, pIndexOld);
        assert(pIndexFork != nullptr);
    }

    // Rollback along the old branch.
    while (pIndexOld != pIndexFork)
    {
        if (pIndexOld->nHeight > 0) // Never disconnect the genesis block.
        {
            CBlock block;
            if (!ReadBlockFromDisk(block, pIndexOld, Params().GetConsensus()))
            {
                return error("RollbackBlock(): ReadBlockFromDisk() failed at %d, hash=%s", pIndexOld->nHeight,
                             pIndexOld->GetBlockHash().ToString());
            }

            mlog.info("Rolling back %s (%i)\n", pIndexOld->GetBlockHash().ToString(), pIndexOld->nHeight);

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
        mlog.info("Rolling forward %s (%i)\n", pIndex->GetBlockHash().ToString(), nHeight);
        CBlock block;
        if (!ReadBlockFromDisk(block, pIndex, Params().GetConsensus()))
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

bool CChainComponent::NeedFullFlush(FlushStateMode mode)
{
    return true;
}

bool CChainComponent::FlushStateToDisk(CValidationState &state, FlushStateMode mode, const CChainParams &chainparams)
{
    LOCK(cs_main);

    static int64_t iLastWrite = 0;
    static int64_t iLastFlush = 0;
    static int64_t iLastSetChain = 0;

    int64_t iNow = GetTimeMicros();

    // Avoid writing/flushing immediately after startup.
    if (iLastWrite == 0)
    {
        iLastWrite = iNow;
    }
    if (iLastFlush == 0)
    {
        iLastFlush = iNow;
    }
    if (iLastSetChain == 0)
    {
        iLastSetChain = iNow;
    }

    GET_TXMEMPOOL_INTERFACE(ifTxMempoolObj);
    int64_t iMempoolUsage = ifTxMempoolObj->GetMemPool().DynamicMemoryUsage();
    int64_t iMempoolSizeMax = Args().GetArg<uint32_t>("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    int64_t iCacheSize = cViewManager.GetCoinsTip()->DynamicMemoryUsage();
    int64_t iTotalSpace = iCoinCacheUsage + std::max<int64_t>(iMempoolSizeMax - iMempoolUsage, 0);

    // The cache is large and we're within 10% and 10 MiB of the limit, but we have time now (not in the middle of a block processing).
    bool bCacheLarge = mode == FLUSH_STATE_PERIODIC && iCacheSize > std::max((9 * iTotalSpace) / 10, iTotalSpace -
                                                                                                     MAX_BLOCK_COINSDB_USAGE *
                                                                                                     1024 * 1024);
    // The cache is over the limit, we have to write now.
    bool bCacheCritical = mode == FLUSH_STATE_IF_NEEDED && iCacheSize > iTotalSpace;
    // It's been a while since we wrote the block index to disk. Do this frequently, so we don't need to redownload after a crash.
    bool bPeriodicWrite =
            mode == FLUSH_STATE_PERIODIC && iNow > iLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
    // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage.
    bool bPeriodicFlush =
            mode == FLUSH_STATE_PERIODIC && iNow > iLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
    // Combine all conditions that result in a full cache flush.
    bool bDoFullFlush = (mode == FLUSH_STATE_ALWAYS) || bCacheLarge || bCacheCritical || bPeriodicFlush;

    // Write blocks and block index to disk.
    if (bDoFullFlush || bPeriodicWrite)
    {
        // Depend on nMinDiskSpace to ensure we can write block index
        if (!CheckDiskSpace(0))
            return state.Error("out of disk space");

        // block file flush
        int iLastBlockFile = cIndexManager.GetLastBlockFile();
        int iSize = cIndexManager.GetBlockFileInfo()[iLastBlockFile].nSize;
        int iUndoSize = cIndexManager.GetBlockFileInfo()[iLastBlockFile].nUndoSize;
        FlushBlockFile(iLastBlockFile, iSize, iUndoSize);

        // block index flush
        if (!cIndexManager.Flush())
        {
            return AbortNode(state, "Failed to write to block index database");
        }

        iLastWrite = iNow;
    }

    // Flush best chain related state. This can only be done if the blocks / block index write was also done.
    if (bDoFullFlush)
    {
        // Typical Coin structures on disk are around 48 bytes in size.
        // Pushing a new one to the database can cause it to be written
        // twice (once in the log, and once in the tables). This is already
        // an overestimation, as most will delete an existing entry or
        // overwrite one. Still, use a conservative safety factor of 2.
        if (!CheckDiskSpace(48 * 2 * 2 * cViewManager.GetCoinsTip()->GetCacheSize()))
        {
            return state.Error("out of disk space");
        }

        // view flush
        if (!cViewManager.Flush())
        {
            return AbortNode(state, "Failed to write to coin database");
        }

        iLastFlush = iNow;
    }

    if (bDoFullFlush || ((mode == FLUSH_STATE_ALWAYS || mode == FLUSH_STATE_PERIODIC) &&
                         iNow > iLastSetChain + (int64_t)DATABASE_WRITE_INTERVAL * 1000000))
    {
        // Update best block in wallet (so we can detect restored wallets).
        GetMainSignals().SetBestChain(cIndexManager.GetChain().GetLocator());
        iLastSetChain = iNow;
    }
    return true;
}

void CChainComponent::FlushStateToDisk()
{
    CValidationState state;
    FlushStateToDisk(state, FLUSH_STATE_ALWAYS, Params());
}

bool CChainComponent::IsInitialBlockDownload()
{
    // Once this function has returned false, it must remain false.
    static std::atomic<bool> latchToFalse{false};
    // Optimization: pre-test latch before taking the lock.
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;

    LOCK(cs);
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;
    if (fImporting || IsReindexing())
        return true;
    if (Tip() == nullptr)
        return true;
    if (Tip()->nChainWork < nMinimumChainWork)
        return true;
    if (Tip()->GetBlockTime() < (GetTime() - nMaxTipAge))
        return true;
    mlog.info("Leaving InitialBlockDownload (latching to false)");
    latchToFalse.store(true, std::memory_order_relaxed);
    return false;
}

void CChainComponent::DoWarning(const std::string &strWarning)
{
    static bool fWarned = false;
    SetMiscWarning(strWarning);
    if (!fWarned)
    {
        AlertNotify(strWarning);
        fWarned = true;
    }
}

/** Update chainActive and related internal data structures. */
void CChainComponent::UpdateTip(CBlockIndex *pindexNew, const CChainParams &chainParams)
{
    CChain &chainActive = cIndexManager.GetChain();

    chainActive.SetTip(pindexNew);

    GET_TXMEMPOOL_INTERFACE(ifMemPoolObj);
    // New best block
    ifMemPoolObj->GetMemPool().AddTransactionsUpdated(1);

    cvBlockChange.notify_all();

    std::vector<std::string> warningMessages;
    if (!IsInitialBlockDownload())
    {
        int nUpgraded = 0;
        const CBlockIndex *pindex = chainActive.Tip();
        for (int bit = 0; bit < VERSIONBITS_NUM_BITS; bit++)
        {
            WarningBitsConditionChecker checker(bit);
            ThresholdState state = checker.GetStateFor(pindex, chainParams.GetConsensus(), warningcache[bit]);
            if (state == THRESHOLD_ACTIVE || state == THRESHOLD_LOCKED_IN)
            {
                const std::string strWarning = strprintf(_("Warning: unknown new rules activated (versionbit %i)"),
                                                         bit);
                if (state == THRESHOLD_ACTIVE)
                {
                    DoWarning(strWarning);
                } else
                {
                    warningMessages.push_back(strWarning);
                }
            }
        }
        // Check the version of the last 100 blocks to see if we need to upgrade:
        for (int i = 0; i < 100 && pindex != nullptr; i++)
        {
            int32_t nExpectedVersion = ComputeBlockVersion(pindex->pprev, chainParams.GetConsensus());
            if (pindex->nVersion > VERSIONBITS_LAST_OLD_BLOCK_VERSION && (pindex->nVersion & ~nExpectedVersion) != 0)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            warningMessages.push_back(strprintf(_("%d of last 100 blocks have unexpected version"), nUpgraded));
        if (nUpgraded > 100 / 2)
        {
            std::string strWarning = _(
                    "Warning: Unknown block versions being mined! It's possible unknown rules are in effect");
            //             notify GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            DoWarning(strWarning);
        }
    }
    mlog_info(
            "new best=%s height=%d version=0x%08x log2_work=%.8g tx=%lu date='%s' progress=%f cache=%.1fMiB(%utxo)", \
            chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(), chainActive.Tip()->nVersion, \
            log(chainActive.Tip()->nChainWork.getdouble()) / log(2.0), (unsigned long)chainActive.Tip()->nChainTx, \
            DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()), \
            GuessVerificationProgress(chainParams.TxData(), chainActive.Tip()), \
            cViewManager.GetCoinsTip()->DynamicMemoryUsage() * (1.0 / (1 << 20)), \
            cViewManager.GetCoinsTip()->GetCacheSize());
    if (!warningMessages.empty())
        mlog_info(" warning='%s'", boost::algorithm::join(warningMessages, ", "));

}

bool CChainComponent::DisconnectTip(CValidationState &state, const CChainParams &chainparams,
                                    DisconnectedBlockTransactions *disconnectpool)
{
    CBlockIndex *pIndexDelete = Tip();
    assert(pIndexDelete);

    // Read block from disk.
    std::shared_ptr<CBlock> pBlock = std::make_shared<CBlock>();
    CBlock &block = *pBlock;

    if (!ReadBlockFromDisk(block, pIndexDelete, Params().GetConsensus()))
    {
        return AbortNode(state, "Failed to read block");
    }
    // Apply the block atomically to the chain state.
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(cViewManager.GetCoinsTip());
        assert(view.GetBestBlock() == pIndexDelete->GetBlockHash());
        if (cViewManager.DisconnectBlock(block, pIndexDelete, view))
        {
            return error("DisconnectTip(): DisconnectBlock %s failed", pIndexDelete->GetBlockHash().ToString());
        }

        bool bFlush = view.Flush();
        assert(bFlush);
    }

    mlog.info("- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);

    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED, chainparams))
    {
        return false;
    }

    if (disconnectpool)
    {
        // Save transactions to re-add to mempool at end of reorg
        for (auto it = block.vtx.rbegin(); it != block.vtx.rend(); ++it)
        {
            disconnectpool->addTransaction(*it);
        }

        GET_TXMEMPOOL_INTERFACE(ifMemPoolObj);
        while (disconnectpool->DynamicMemoryUsage() > MAX_DISCONNECTED_TX_POOL_SIZE * 1000)
        {
            // Drop the earliest entry, and remove its children from the mempool.
            auto it = disconnectpool->queuedTx.get<insertion_order>().begin();
            ifMemPoolObj->GetMemPool().removeRecursive(**it, MemPoolRemovalReason::REORG);
            disconnectpool->removeEntry(it);
        }
    }

    // Update chainActive and related variables.
    UpdateTip(pIndexDelete->pprev, chainparams);

    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    GetMainSignals().BlockDisconnected(pBlock);
    return true;
}

static bool
CheckBlockHeader(const CBlockHeader &block, CValidationState &state, const Consensus::Params &consensusParams,
                 bool fCheckPOW = true)
{
    // Check proof of work matches claimed amount
    if (fCheckPOW && !CheckProofOfWork(block.GetHash(), block.nBits, consensusParams))
        return state.DoS(50, false, REJECT_INVALID, "high-hash", false, "proof of work failed");

    return true;
}

bool CChainComponent::CheckBlock(const CBlock &block, CValidationState &state, const Consensus::Params &consensusParams,
                                 bool fCheckPOW,
                                 bool fCheckMerkleRoot)
{
    // These are checks that are independent of context.

    if (block.fChecked)
        return true;

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, consensusParams, fCheckPOW))
        return false;

    // Check the merkle root.
    if (fCheckMerkleRoot)
    {
        bool mutated;
        uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS(100, false, REJECT_INVALID, "bad-txnmrklroot", true, "hashMerkleRoot mismatch");

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (mutated)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-duplicate", true, "duplicate transaction");
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.
    // Note that witness malleability is checked in ContextualCheckBlock, so no
    // checks that use witness data may be performed here.

    // Size limits
    if (block.vtx.empty() || block.vtx.size() * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT ||
        ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) *
        WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-length", false, "size limits failed");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-missing", false, "first tx is not coinbase");
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i]->IsCoinBase())
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-multiple", false, "more than one coinbase");

    // Check transactions
    for (const auto &tx : block.vtx)
    {
        if (!tx->CheckTransaction(state, false))
        {
            return state.Invalid(false, state.GetRejectCode(), state.GetRejectReason(),
                                 strprintf("Transaction check failed (tx hash %s) %s", tx->GetHash().ToString(),
                                           state.GetDebugMessage()));
        }

    }

    unsigned int nSigOps = 0;
    for (const auto &tx : block.vtx)
    {
        nSigOps += tx->GetLegacySigOpCount();
    }
    if (nSigOps * WITNESS_SCALE_FACTOR > MAX_BLOCK_SIGOPS_COST)
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-sigops", false, "out-of-bounds SigOpCount");

    if (fCheckPOW && fCheckMerkleRoot)
        block.fChecked = true;

    return true;
}

static bool IsSBTCForkEnabled(const Consensus::Params &params, const CBlockIndex *pindex)
{
    return pindex->nHeight >= params.SBTCForkHeight;
}

bool CChainComponent::IsSBTCForkEnabled(const int height)
{
    return height >= Params().GetConsensus().SBTCForkHeight;
}

bool CChainComponent::IsSBTCForkHeight(const Consensus::Params &params, const int &height)
{
    return params.SBTCForkHeight == height;
}

static unsigned int GetBlockScriptFlags(const CBlockIndex *pindex, const Consensus::Params &consensusparams)
{
    AssertLockHeld(cs_main);

    // BIP16 didn't become active until Apr 1 2012
    int64_t nBIP16SwitchTime = 1333238400;
    bool fStrictPayToScriptHash = (pindex->GetBlockTime() >= nBIP16SwitchTime);

    unsigned int flags = fStrictPayToScriptHash ? SCRIPT_VERIFY_P2SH : SCRIPT_VERIFY_NONE;

    // Start enforcing the DERSIG (BIP66) rule
    if (pindex->nHeight >= consensusparams.BIP66Height)
    {
        flags |= SCRIPT_VERIFY_DERSIG;
    }

    // Start enforcing CHECKLOCKTIMEVERIFY (BIP65) rule
    if (pindex->nHeight >= consensusparams.BIP65Height)
    {
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic.
    if (VersionBitsState(pindex->pprev, consensusparams, Consensus::DEPLOYMENT_CSV, versionbitscache) ==
        THRESHOLD_ACTIVE)
    {
        flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
    }

    // Start enforcing WITNESS rules using versionbits logic.
    if (IsWitnessEnabled(pindex->pprev, consensusparams))
    {
        flags |= SCRIPT_VERIFY_WITNESS;
        flags |= SCRIPT_VERIFY_NULLDUMMY;
    }

    // If the sbtc fork is enabled
    if (IsSBTCForkEnabled(consensusparams, pindex->pprev))
    {
        flags |= SCRIPT_ENABLE_SIGHASH_SBTC_FORK;
    }

    return flags;
}

bool CChainComponent::ContextualCheckBlock(const CBlock &block, CValidationState &state,
                                           const Consensus::Params &consensusParams,
                                           const CBlockIndex *pindexPrev)
{
    const int nHeight = pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1;

    // Start enforcing BIP113 (Median Time Past) using versionbits logic.
    int nLockTimeFlags = 0;
    if (VersionBitsState(pindexPrev, consensusParams, Consensus::DEPLOYMENT_CSV, versionbitscache) == THRESHOLD_ACTIVE)
    {
        nLockTimeFlags |= LOCKTIME_MEDIAN_TIME_PAST;
    }

    int64_t nLockTimeCutoff = (nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST)
                              ? pindexPrev->GetMedianTimePast()
                              : block.GetBlockTime();

    // Check that all transactions are finalized
    for (const auto &tx : block.vtx)
    {
        if (!tx->IsFinalTx(nHeight, nLockTimeCutoff))
        {
            return state.DoS(10, false, REJECT_INVALID, "bad-txns-nonfinal", false, "non-final transaction");
        }
    }

    // Enforce rule that the coinbase starts with serialized block height
    if (nHeight >= consensusParams.BIP34Height)
    {
        CScript expect = CScript() << nHeight;
        if (block.vtx[0]->vin[0].scriptSig.size() < expect.size() ||
            !std::equal(expect.begin(), expect.end(), block.vtx[0]->vin[0].scriptSig.begin()))
        {
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-height", false, "block height mismatch in coinbase");
        }
    }

    // Validation for witness commitments.
    // * We compute the witness hash (which is the hash including witnesses) of all the block's transactions, except the
    //   coinbase (where 0x0000....0000 is used instead).
    // * The coinbase scriptWitness is a stack of a single 32-byte vector, containing a witness nonce (unconstrained).
    // * We build a merkle tree with all those witness hashes as leaves (similar to the hashMerkleRoot in the block header).
    // * There must be at least one output whose scriptPubKey is a single 36-byte push, the first 4 bytes of which are
    //   {0xaa, 0x21, 0xa9, 0xed}, and the following 32 bytes are SHA256^2(witness root, witness nonce). In case there are
    //   multiple, the last one is used.
    bool fHaveWitness = false;
    if (VersionBitsState(pindexPrev, consensusParams, Consensus::DEPLOYMENT_SEGWIT, versionbitscache) ==
        THRESHOLD_ACTIVE)
    {
        int commitpos = ::GetWitnessCommitmentIndex(block);
        if (commitpos != -1)
        {
            bool malleated = false;
            uint256 hashWitness = BlockWitnessMerkleRoot(block, &malleated);
            // The malleation check is ignored; as the transaction tree itself
            // already does not permit it, it is impossible to trigger in the
            // witness tree.
            if (block.vtx[0]->vin[0].scriptWitness.stack.size() != 1 ||
                block.vtx[0]->vin[0].scriptWitness.stack[0].size() != 32)
            {
                return state.DoS(100, false, REJECT_INVALID, "bad-witness-nonce-size", true,
                                 strprintf("%s : invalid witness nonce size", __func__));
            }
            CHash256().Write(hashWitness.begin(), 32).Write(&block.vtx[0]->vin[0].scriptWitness.stack[0][0],
                                                            32).Finalize(hashWitness.begin());
            if (memcmp(hashWitness.begin(), &block.vtx[0]->vout[commitpos].scriptPubKey[6], 32))
            {
                return state.DoS(100, false, REJECT_INVALID, "bad-witness-merkle-match", true,
                                 strprintf("%s : witness merkle commitment mismatch", __func__));
            }
            fHaveWitness = true;
        }
    }

    // No witness data is allowed in blocks that don't commit to witness data, as this would otherwise leave room for spam
    if (!fHaveWitness)
    {
        for (const auto &tx : block.vtx)
        {
            if (tx->HasWitness())
            {
                return state.DoS(100, false, REJECT_INVALID, "unexpected-witness", true,
                                 strprintf("%s : unexpected witness data found", __func__));
            }
        }
    }

    // After the coinbase witness nonce and commitment are verified,
    // we can check if the block weight passes (before we've checked the
    // coinbase witness, it would be possible for the weight to be too
    // large by filling up the coinbase witness, which doesn't change
    // the block hash, so we couldn't mark the block as permanently
    // failed).
    if (GetBlockWeight(block) > MAX_BLOCK_WEIGHT)
    {
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-weight", false,
                         strprintf("%s : weight limit failed", __func__));
    }

    return true;
}

/** Apply the effects of this block (with given index) on the UTXO set represented by coins.
 *  Validity checks that depend on the UTXO set are also done; ConnectBlock()
 *  can fail if those validity checks fail (among other reasons).
*/
bool
CChainComponent::ConnectBlock(const CBlock &block, CValidationState &state, CBlockIndex *pindex, CCoinsViewCache &view,
                              const CChainParams &chainparams, bool fJustCheck)
{
    AssertLockHeld(cs);
    assert(pindex);
    // pindex->phashBlock can be null if called by CreateNewBlock/TestBlockValidity
    assert((pindex->phashBlock == nullptr) || (*pindex->phashBlock == block.GetHash()));
    int64_t nTimeStart = GetTimeMicros();

    // Check it again in case a previous version let a bad block in
    if (!CheckBlock(block, state, chainparams.GetConsensus(), !fJustCheck, !fJustCheck))
        return error("%s: Consensus::CheckBlock: %s", __func__, FormatStateMessage(state));

    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = pindex->pprev == nullptr ? uint256() : pindex->pprev->GetBlockHash();
    assert(hashPrevBlock == view.GetBestBlock());

    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if (block.GetHash() == chainparams.GetConsensus().hashGenesisBlock)
    {
        if (!fJustCheck)
            view.SetBestBlock(pindex->GetBlockHash());
        return true;
    }

    bool fScriptChecks = true;
    if (!hashAssumeValid.IsNull())
    {
        int64_t etime = cIndexManager.GetBlockProofEquivalentTime(hashAssumeValid, pindex, chainparams);
        fScriptChecks = (etime <= 60 * 60 * 24 * 7 * 2);
    }

    int64_t nTime1 = GetTimeMicros();
    nTimeCheck += nTime1 - nTimeStart;
    mlog.info("- Sanity checks: %.2fms [%.2fs]\n", 0.001 * (nTime1 - nTimeStart), nTimeCheck * 0.000001);

    // Do not allow blocks that contain transactions which 'overwrite' older transactions,
    // unless those are already completely spent.
    // If such overwrites are allowed, coinbases and transactions depending upon those
    // can be duplicated to remove the ability to spend the first instance -- even after
    // being sent to another address.
    // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
    // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
    // already refuses previously-known transaction ids entirely.
    // This rule was originally applied to all blocks with a timestamp after March 15, 2012, 0:00 UTC.
    // Now that the whole chain is irreversibly beyond that time it is applied to all blocks except the
    // two in the chain that violate it. This prevents exploiting the issue against nodes during their
    // initial block download.
    bool fEnforceBIP30 = (!pindex->phashBlock) || // Enforce on CreateNewBlock invocations which don't have a hash.
                         !((pindex->nHeight == 91842 && pindex->GetBlockHash() == uint256S(
                                 "0x00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec")) ||
                           (pindex->nHeight == 91880 && pindex->GetBlockHash() == uint256S(
                                   "0x00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721")));

    // Once BIP34 activated it was not possible to create new duplicate coinbases and thus other than starting
    // with the 2 existing duplicate coinbase pairs, not possible to create overwriting txs.  But by the
    // time BIP34 activated, in each of the existing pairs the duplicate coinbase had overwritten the first
    // before the first had been spent.  Since those coinbases are sufficiently buried its no longer possible to create further
    // duplicate transactions descending from the known pairs either.
    // If we're on the known chain at height greater than where BIP34 activated, we can save the db accesses needed for the BIP30 check.
    CBlockIndex *pindexBIP34height = pindex->pprev->GetAncestor(chainparams.GetConsensus().BIP34Height);
    //Only continue to enforce if we're below BIP34 activation height or the block hash at that height doesn't correspond.
    fEnforceBIP30 = fEnforceBIP30 && (!pindexBIP34height ||
                                      !(pindexBIP34height->GetBlockHash() == chainparams.GetConsensus().BIP34Hash));

    if (fEnforceBIP30)
    {
        for (const auto &tx : block.vtx)
        {
            for (size_t o = 0; o < tx->vout.size(); o++)
            {
                if (view.HaveCoin(COutPoint(tx->GetHash(), o)))
                {
                    return state.DoS(100, error("ConnectBlock(): tried to overwrite transaction"),
                                     REJECT_INVALID, "bad-txns-BIP30");
                }
            }
        }
    }

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic.
    int nLockTimeFlags = 0;
    if (VersionBitsState(pindex->pprev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_CSV, versionbitscache) ==
        THRESHOLD_ACTIVE)
    {
        nLockTimeFlags |= LOCKTIME_VERIFY_SEQUENCE;
    }

    // Get the script flags for this block
    unsigned int flags = GetBlockScriptFlags(pindex, chainparams.GetConsensus());

    int64_t nTime2 = GetTimeMicros();
    nTimeForks += nTime2 - nTime1;
    mlog.info("    - Fork checks: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeForks * 0.000001);

    CBlockUndo blockundo;

    CCheckQueueControl<CScriptCheck> control(fScriptChecks && nScriptCheckThreads ? &scriptCheckQueue : nullptr);

    std::vector<int> prevheights;
    CAmount nFees = 0;
    int nInputs = 0;
    int64_t nSigOpsCost = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    std::vector<PrecomputedTransactionData> txdata;
    // Required so that pointers to individual PrecomputedTransactionData don't get invalidated
    txdata.reserve(block.vtx.size());
    for (unsigned int i = 0; i < block.vtx.size(); i++)
    {
        const CTransaction &tx = *(block.vtx[i]);

        nInputs += tx.vin.size();

        if (!tx.IsCoinBase())
        {
            if (!view.HaveInputs(tx))
                return state.DoS(100, error("ConnectBlock(): inputs missing/spent"),
                                 REJECT_INVALID, "bad-txns-inputs-missingorspent");

            // Check that transaction is BIP68 final
            // BIP68 lock checks (as opposed to nLockTime checks) must
            // be in ConnectBlock because they require the UTXO set
            prevheights.resize(tx.vin.size());
            for (size_t j = 0; j < tx.vin.size(); j++)
            {
                prevheights[j] = view.AccessCoin(tx.vin[j].prevout).nHeight;
            }

            if (!tx.SequenceLocks(nLockTimeFlags, &prevheights, *pindex))
            {
                return state.DoS(100, error("%s: contains a non-BIP68-final transaction", __func__),
                                 REJECT_INVALID, "bad-txns-nonfinal");
            }
        }

        // GetTransactionSigOpCost counts 3 types of sigops:
        // * legacy (always)
        // * p2sh (when P2SH enabled in flags and excludes coinbase)
        // * witness (when witness enabled in flags and excludes coinbase)
        nSigOpsCost += tx.GetTransactionSigOpCost(view, flags);
        if (nSigOpsCost > MAX_BLOCK_SIGOPS_COST)
            return state.DoS(100, error("ConnectBlock(): too many sigops"),
                             REJECT_INVALID, "bad-blk-sigops");

        txdata.emplace_back(tx);
        if (!tx.IsCoinBase())
        {
            nFees += view.GetValueIn(tx) - tx.GetValueOut();

            std::vector<CScriptCheck> vChecks;
            bool fCacheResults = fJustCheck; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
            if (!tx.CheckInputs(state, view, fScriptChecks, flags, fCacheResults, fCacheResults, txdata[i],
                                nScriptCheckThreads ? &vChecks : nullptr))
                return error("ConnectBlock(): CheckInputs on %s failed with %s",
                             tx.GetHash().ToString(), FormatStateMessage(state));
            control.Add(vChecks);
        }

        CTxUndo undoDummy;
        if (i > 0)
        {
            blockundo.vtxundo.push_back(CTxUndo());
        }
        cViewManager.UpdateCoins(tx, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);

        vPos.push_back(std::make_pair(tx.GetHash(), pos));
        pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
    }
    int64_t nTime3 = GetTimeMicros();
    nTimeConnect += nTime3 - nTime2;
    mlog_error("- Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n",
               (unsigned)block.vtx.size(), 0.001 * (nTime3 - nTime2), 0.001 * (nTime3 - nTime2) / block.vtx.size(),
               nInputs <= 1 ? 0 : 0.001 * (nTime3 - nTime2) / (nInputs - 1), nTimeConnect * 0.000001);

    CAmount blockReward = 0;
    blockReward = nFees + GetBlockSubsidy(pindex->nHeight);
    if (block.vtx[0]->GetValueOut() > blockReward)
        return state.DoS(100,
                         error("ConnectBlock(): coinbase pays too much (actual=%d vs limit=%d)",
                               block.vtx[0]->GetValueOut(), blockReward),
                         REJECT_INVALID, "bad-cb-amount");

    if (!control.Wait())
        return state.DoS(100, error("%s: CheckQueue failed", __func__), REJECT_INVALID, "block-validation-failed");
    int64_t nTime4 = GetTimeMicros();
    nTimeVerify += nTime4 - nTime2;
    mlog_error("- Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1,
               0.001 * (nTime4 - nTime2), nInputs <= 1 ? 0 : 0.001 * (nTime4 - nTime2) / (nInputs - 1),
               nTimeVerify * 0.000001);

    if (fJustCheck)
        return true;

    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS))
    {
        if (pindex->GetUndoPos().IsNull())
        {
            CDiskBlockPos _pos;
            if (!cIndexManager.FindUndoPos(state, pindex->nFile, _pos,
                                           ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                return error("ConnectBlock(): FindUndoPos failed");
            if (!UndoWriteToDisk(blockundo, _pos, pindex->pprev->GetBlockHash(), chainparams.MessageStart()))
                return AbortNode(state, "Failed to write undo data");

            // update nUndoPos in block index
            pindex->nUndoPos = _pos.nPos;
            pindex->nStatus |= BLOCK_HAVE_UNDO;
        }

        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        cIndexManager.SetDirtyIndex(pindex);
    }

    if (IsTxIndex())
        if (!GetBlockTreeDB()->WriteTxIndex(vPos))
            return AbortNode(state, "Failed to write transaction index");

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    int64_t nTime5 = GetTimeMicros();
    nTimeIndex += nTime5 - nTime4;
    mlog_error("    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime5 - nTime4), nTimeIndex * 0.000001);

    int64_t nTime6 = GetTimeMicros();
    nTimeCallbacks += nTime6 - nTime5;
    mlog_error("    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime6 - nTime5), nTimeCallbacks * 0.000001);
    return true;
}

/**
 * Connect a new block to chainActive. pblock is either nullptr or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 *
 * The block is added to connectTrace if connection succeeds.
 */
bool CChainComponent::ConnectTip(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pIndexNew,
                                 const std::shared_ptr<const CBlock> &pblock, ConnectTrace &connectTrace,
                                 DisconnectedBlockTransactions &disconnectpool)
{
    assert(pIndexNew->pprev == Tip());
    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    std::shared_ptr<const CBlock> pthisBlock;
    if (!pblock)
    {
        std::shared_ptr<CBlock> pblockNew = std::make_shared<CBlock>();
        if (!ReadBlockFromDisk(*pblockNew, pIndexNew, chainparams.GetConsensus()))
            return AbortNode(state, "Failed to read block");
        pthisBlock = pblockNew;
    } else
    {
        pthisBlock = pblock;
    }
    const CBlock &blockConnecting = *pthisBlock;
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros();
    nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    mlog_error("- Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001,
               nTimeReadFromDisk * 0.000001);
    {
        CCoinsViewCache view(cViewManager.GetCoinsTip());
        bool rv = ConnectBlock(blockConnecting, state, pIndexNew, view, chainparams);
        GetMainSignals().BlockChecked(blockConnecting, state);
        if (!rv)
        {
            if (state.IsInvalid())
                cIndexManager.InvalidBlockFound(pIndexNew, state);
            return error("ConnectTip(): ConnectBlock %s failed", pIndexNew->GetBlockHash().ToString());
        }
        nTime3 = GetTimeMicros();
        nTimeConnectTotal += nTime3 - nTime2;
        mlog_error("  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001,
                   nTimeConnectTotal * 0.000001);
        bool flushed = view.Flush();
        assert(flushed);
    }
    int64_t nTime4 = GetTimeMicros();
    nTimeFlush += nTime4 - nTime3;
    mlog_error("  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED, chainparams))
        return false;
    int64_t nTime5 = GetTimeMicros();
    nTimeChainState += nTime5 - nTime4;
    mlog_error("  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001,
               nTimeChainState * 0.000001);

    // Remove conflicting transactions from the mempool.;
    GET_TXMEMPOOL_INTERFACE(ifMemPoolObj);
    ifMemPoolObj->GetMemPool().removeForBlock(blockConnecting.vtx, pIndexNew->nHeight);
    disconnectpool.removeForBlock(blockConnecting.vtx);
    // Update chainActive & related variables.
    UpdateTip(pIndexNew, chainparams);

    int64_t nTime6 = GetTimeMicros();
    nTimePostConnect += nTime6 - nTime5;
    nTimeTotal += nTime6 - nTime1;
    mlog_error("  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001,
               nTimePostConnect * 0.000001);
    mlog_error("- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);

    connectTrace.BlockConnected(pIndexNew, std::move(pthisBlock));
    return true;
}

void CChainComponent::CheckForkWarningConditions()
{
    AssertLockHeld(cs);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before finishing our initial sync)
    if (IsInitialBlockDownload())
        return;

    cIndexManager.CheckForkWarningConditions();
}

bool CChainComponent::ActivateBestChainStep(CValidationState &state, const CChainParams &chainparams,
                                            CBlockIndex *pIndexMostWork,
                                            const std::shared_ptr<const CBlock> &pblock, bool &bInvalidFound,
                                            ConnectTrace &connectTrace)
{
    AssertLockHeld(cs);
    const CBlockIndex *pIndexOldTip = Tip();
    const CBlockIndex *pIndexFork = cIndexManager.GetChain().FindFork(pIndexMostWork);

    GET_TXMEMPOOL_INTERFACE(ifMemPoolObj);

    // Disconnect active blocks which are no longer in the best chain.
    bool bBlocksDisconnected = false;
    DisconnectedBlockTransactions disconnectPool;
    while (Tip() && Tip() != pIndexFork)
    {
        if (!DisconnectTip(state, chainparams, &disconnectPool))
        {
            // This is likely a fatal error, but keep the mempool consistent,
            // just in case. Only remove from the mempool in this case.
            ifMemPoolObj->GetMemPool().UpdateMempoolForReorg(disconnectPool, false);
            return false;
        }
        bBlocksDisconnected = true;
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex *> vecIndexToConnect;
    bool bContinue = true;
    int iHeight = pIndexFork ? pIndexFork->nHeight : -1;
    while (bContinue && iHeight != pIndexMostWork->nHeight)
    {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int iTargetHeight = std::min(iHeight + 32, pIndexMostWork->nHeight);
        vecIndexToConnect.clear();
        vecIndexToConnect.reserve(iTargetHeight - iHeight);
        CBlockIndex *pIndexIter = pIndexMostWork->GetAncestor(iTargetHeight);
        while (pIndexIter && pIndexIter->nHeight != iHeight)
        {
            vecIndexToConnect.push_back(pIndexIter);
            pIndexIter = pIndexIter->pprev;
        }
        iHeight = iTargetHeight;

        // Connect new blocks.
        for (CBlockIndex *pIndexConnect : reverse_iterate(vecIndexToConnect))
        {
            if (!ConnectTip(state, chainparams, pIndexConnect,
                            pIndexConnect == pIndexMostWork ? pblock : std::shared_ptr<const CBlock>(), connectTrace,
                            disconnectPool))
            {
                if (state.IsInvalid())
                {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                    {
                        cIndexManager.InvalidChainFound(vecIndexToConnect.back());
                    }
                    state = CValidationState();
                    bInvalidFound = true;
                    bContinue = false;
                    break;
                } else
                {
                    // A system error occurred (disk space, database error, ...).
                    // Make the mempool consistent with the current tip, just in case
                    // any observers try to use it before shutdown.
                    //                    UpdateMempoolForReorg(disconnectpool, false);
                    ifMemPoolObj->GetMemPool().UpdateMempoolForReorg(disconnectPool, false);
                    return false;
                }
            } else
            {
                cIndexManager.PruneBlockIndexCandidates();
                if (!pIndexOldTip || Tip()->nChainWork > pIndexOldTip->nChainWork)
                {
                    // We're in a better position than we were. Return temporarily to release the lock.
                    bContinue = false;
                    break;
                }
            }
        }
    }

    if (bBlocksDisconnected)
    {
        // If any blocks were disconnected, disconnectpool may be non empty.  Add
        // any disconnected transactions back to the mempool.
        ifMemPoolObj->GetMemPool().UpdateMempoolForReorg(disconnectPool, true);
    }

    ifMemPoolObj->GetMemPool().Check(cViewManager.GetCoinsTip());

    // Callbacks/notifications for a new best chain.
    if (bInvalidFound)
    {
        cIndexManager.CheckForkWarningConditionsOnNewFork(vecIndexToConnect.back());
    } else
    {
        CheckForkWarningConditions();
    }

    return true;
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either nullptr or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool CChainComponent::ActivateBestChain(CValidationState &state, const CChainParams &chainparams,
                                        std::shared_ptr<const CBlock> pblock)
{
    // Note that while we're often called here from ProcessNewBlock, this is
    // far from a guarantee. Things in the P2P/RPC will often end up calling
    // us in the middle of ProcessNewBlock - do not assume pblock is set
    // sanely for performance or correctness!

    CBlockIndex *pIndexMostWork = nullptr;
    CBlockIndex *pIndexNewTip = nullptr;

    GET_TXMEMPOOL_INTERFACE(ifMemPoolObj);

    int iStopAtHeight = Args().GetArg<int>("-stopatheight", DEFAULT_STOPATHEIGHT);
    do
    {
        boost::this_thread::interruption_point();
        if (app().ShutdownRequested())
            break;

        const CBlockIndex *pIndexFork;
        bool bInitialDownload;
        {
            LOCK(cs);
            ConnectTrace connectTrace(ifMemPoolObj->GetMemPool());

            CBlockIndex *pIndexOldTip = Tip();
            if (pIndexMostWork == nullptr)
            {
                pIndexMostWork = cIndexManager.FindMostWorkIndex();
            }

            // Whether we have anything to do at all.
            if (pIndexMostWork == nullptr || pIndexMostWork == pIndexOldTip)
            {
                return true;
            }

            bool bInvalidFound = false;
            std::shared_ptr<const CBlock> nullBlockPtr;
            if (!ActivateBestChainStep(state, chainparams, pIndexMostWork,
                                       pblock && pblock->GetHash() == pIndexMostWork->GetBlockHash() ? pblock
                                                                                                     : nullBlockPtr,
                                       bInvalidFound, connectTrace))
            {
                return false;
            }

            if (bInvalidFound)
            {
                // Wipe cache, we may need another branch now.
                pIndexMostWork = nullptr;
            }
            pIndexNewTip = Tip();
            pIndexFork = cIndexManager.GetChain().FindFork(pIndexOldTip);
            bInitialDownload = IsInitialBlockDownload();

            // block connect event
            for (const PerBlockConnectTrace &trace : connectTrace.GetBlocksConnected())
            {
                assert(trace.pblock && trace.pindex);
                GetMainSignals().BlockConnected(trace.pblock, trace.pindex, *trace.conflictedTxs);
            }
        }
        // When we reach this point, we switched to a new tip (stored in pindexNewTip).

        // Notifications/callbacks that can run without cs_main

        // Notify external listeners about the new tip.
        GetMainSignals().UpdatedBlockTip(pIndexNewTip, pIndexFork, bInitialDownload);

        // Always notify the UI if a new block tip was connected
        if (pIndexFork != pIndexNewTip)
        {
            uiInterface.NotifyBlockTip(bInitialDownload, pIndexNewTip);
        }

        if (iStopAtHeight && pIndexNewTip && pIndexNewTip->nHeight >= iStopAtHeight)
            app().RequestShutdown();

    } while (pIndexNewTip != pIndexMostWork);

    cIndexManager.CheckBlockIndex(Params().GetConsensus());

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(state, FLUSH_STATE_PERIODIC, Params()))
    {
        return false;
    }

    return true;
}

bool CChainComponent::InvalidateBlock(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pindex)
{
    AssertLockHeld(cs);

    // We first disconnect backwards and then mark the blocks as invalid.
    // This prevents a case where pruned nodes may fail to invalidateblock
    // and be left unable to start as they have no tip candidates (as there
    // are no blocks that meet the "have data and are not invalid per
    // nStatus" criteria for inclusion in setBlockIndexCandidates).

    bool pindex_was_in_chain = false;
    CChain &chainActive = cIndexManager.GetChain();

    CBlockIndex *invalid_walk_tip = chainActive.Tip();

    GET_TXMEMPOOL_INTERFACE(ifMemPoolObj);

    DisconnectedBlockTransactions disconnectpool;
    while (chainActive.Contains(pindex))
    {
        pindex_was_in_chain = true;
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state, chainparams, &disconnectpool))
        {
            // It's probably hopeless to try to make the mempool consistent
            // here if DisconnectTip failed, but we can try.
            ifMemPoolObj->GetMemPool().UpdateMempoolForReorg(disconnectpool, false);
            return false;
        }
    }

    // Now mark the blocks we just disconnected as descendants invalid
    // (note this may not be all descendants).
    cIndexManager.InvalidateBlock(pindex, invalid_walk_tip, pindex_was_in_chain);

    // DisconnectTip will add transactions to disconnectpool; try to add these
    // back to the mempool.
    ifMemPoolObj->GetMemPool().UpdateMempoolForReorg(disconnectpool, true);

    cIndexManager.InvalidChainFound(pindex);
    uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindex->pprev);
    return true;
}

bool CChainComponent::CheckActiveChain(CValidationState &state, const CChainParams &chainparams)
{
    LOCK(cs);

    CBlockIndex *pOldTipIndex = Tip();  // 1. current block chain tip
    mlog.info("Current tip block:%s\n", pOldTipIndex->ToString().c_str());
    MapCheckpoints checkpoints = chainparams.Checkpoints().mapCheckpoints;

    if (checkpoints.rbegin()->first < 1)
        return true;

    CChain &chainActive = cIndexManager.GetChain();
    if (chainActive.Height() >= checkpoints.rbegin()->first &&
        chainActive[checkpoints.rbegin()->first]->GetBlockHash() == checkpoints.rbegin()->second)
    {
        return true;
    }


    auto GetFirstCheckPointInChain = [&]()
    {
        auto itReturn = checkpoints.rbegin();
        for (; itReturn != checkpoints.rend(); itReturn++)
        {
            if (chainActive[itReturn->first] != nullptr &&
                chainActive[itReturn->first]->GetBlockHash() == itReturn->second)
            {
                break;
            }
        }
        return itReturn;
    };

    auto GetFirstCheckPointNotInChain = [&](MapCheckpoints::const_reverse_iterator firstInChain)
    {
        assert(firstInChain != checkpoints.rbegin());
        firstInChain--;
        assert(chainActive[firstInChain->first] == nullptr ||
               chainActive[firstInChain->first]->GetBlockHash() != firstInChain->second);
        return firstInChain;
    };

    auto firstInChainPoint = GetFirstCheckPointInChain();
    assert(checkpoints.rbegin() != firstInChainPoint);
    auto firstNotInChainPoint = GetFirstCheckPointNotInChain(firstInChainPoint);


    if (firstNotInChainPoint->first > chainActive.Height())
    {
        return true;
    }

    CBlockIndex *desPoint = chainActive[firstNotInChainPoint->first];


    if (!InvalidateBlock(state, chainparams, desPoint))
    {
        return false;
    }

    if (state.IsValid())
    {
        ActivateBestChain(state, chainparams, nullptr);
    }

    if (!state.IsValid())
    {

        mlog_error("reject reason %s", state.GetRejectReason());
        return false;

    }


    if (chainActive.Tip() != pOldTipIndex)
    {
        // Write changes  to disk.
        if (!FlushStateToDisk(state, FLUSH_STATE_ALWAYS, chainparams))
        {
            return false;
        }
        uiInterface.NotifyBlockTip(IsInitialBlockDownload(), chainActive.Tip());
    }

    mlog.info("CheckActiveChain End====");

    return true;
}

bool CChainComponent::RewindBlock(const CChainParams &params)
{
    LOCK(cs);

    // Note that during -reindex-chainstate we are called with an empty chainActive!

    int iHeight = 1;
    CChain &chainActive = cIndexManager.GetChain();
    while (iHeight <= chainActive.Height())
    {
        if (cIndexManager.NeedRewind(iHeight, params.GetConsensus()))
        {
            break;
        }
        iHeight++;
    }

    // nHeight is now the height of the first insufficiently-validated block, or tipheight + 1
    CValidationState state;
    CBlockIndex *pIndex = chainActive.Tip();
    while (chainActive.Height() >= iHeight)
    {
        if (fPruneMode && !(chainActive.Tip()->nStatus & BLOCK_HAVE_DATA))
        {
            // If pruning, don't try rewinding past the HAVE_DATA point;
            // since older blocks can't be served anyway, there's
            // no need to walk further, and trying to DisconnectTip()
            // will fail (and require a needless reindex/redownload
            // of the blockchain).
            break;
        }
        if (!DisconnectTip(state, params, nullptr))
        {
            return error("RewindBlock: unable to disconnect block at height %i", pIndex->nHeight);
        }
        // Occasionally flush state to disk.
        if (!FlushStateToDisk(state, FLUSH_STATE_PERIODIC, params))
        {
            return false;
        }
    }

    // Reduce validity flag and have-data flags.
    // We do this after actual disconnecting, otherwise we'll end up writing the lack of data
    // to disk before writing the chainstate, resulting in a failure to continue if interrupted.
    cIndexManager.RewindBlockIndex(params.GetConsensus());

    if (Tip() != nullptr)
    {
        if (!FlushStateToDisk(state, FLUSH_STATE_ALWAYS, params))
        {
            return false;
        }
    }

    return true;
}

bool CChainComponent::LoadChainTip(const CChainParams &chainparams)
{
    CChain &chainActive = cIndexManager.GetChain();
    uint256 bestHash = cViewManager.GetCoinsTip()->GetBestBlock();
    if (chainActive.Tip() && chainActive.Tip()->GetBlockHash() == bestHash)
    {
        return true;
    }

    if (bestHash.IsNull() && cIndexManager.IsOnlyGenesisBlockIndex())
    {
        // In case we just added the genesis block, connect it now, so
        // that we always have a chainActive.Tip() when we return.
        mlog.info("%s: Connecting genesis block...", __func__);
        CValidationState state;
        if (!ActivateBestChain(state, chainparams, nullptr))
        {
            return false;
        }
    }

    // Load pointer to end of best chain
    CBlockIndex *pTip = cIndexManager.GetBlockIndex(bestHash);
    if (!pTip)
    {
        return false;
    }

    SetTip(pTip);

    cIndexManager.PruneBlockIndexCandidates();

    mlog.info("Loaded best chain: hashBestChain=%s height=%d date=%s progress=%f\n",
              chainActive.Tip()->GetBlockHash().ToString().c_str(), chainActive.Height(),
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()).c_str(),
              GuessVerificationProgress(chainparams.TxData(), chainActive.Tip()));
    return true;
}

int CChainComponent::VerifyBlocks()
{
    uiInterface.InitMessage(_("Verifying blocks..."));

    LOCK(cs);
    CBlockIndex *tip = Tip();
    if (tip && tip->nTime > GetAdjustedTime() + 2 * 60 * 60)
    {
        return ERR_FUTURE_BLOCK;
    }

    uint32_t checkLevel = Args().GetArg<uint32_t>("-checklevel", DEFAULT_CHECKLEVEL);
    int checkBlocks = Args().GetArg<int>("-checkblocks", DEFAULT_CHECKBLOCKS);
    if (!VerifyDB(Params(), cViewManager.GetCoinViewDB(), checkLevel, checkBlocks))
    {
        return ERR_VERIFY_DB;
    }

    return OK_CHAIN;
}

void CChainComponent::ThreadImport()
{
    RenameThread("bitcoin-loadblk");

    if (bReIndex)
    {
        int iFile = 0;
        while (true)
        {
            CDiskBlockPos pos(iFile, 0);
            if (!fs::exists(GetBlockPosFilename(pos, "blk")))
                break; // No block files left to reindex
            FILE *file = OpenBlockFile(pos, true);
            if (!file)
                break; // This error is logged in OpenBlockFile
            mlog.info("Reindexing block file blk%05u.dat...", (unsigned int)iFile);
            LoadExternalBlockFile(Params(), file, &pos);
            iFile++;
        }

        mlog.info("Reindexing finished");
    }

    if (cIndexManager.NeedInitGenesisBlock(Params()))
    {
        if (!LoadGenesisBlock(Params()))
        {
            return;
        }
    }

    // hardcoded $DATADIR/bootstrap.dat
    fs::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (fs::exists(pathBootstrap))
    {
        FILE *file = fsbridge::fopen(pathBootstrap, "rb");
        if (file)
        {
            fs::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            mlog.info("Importing bootstrap.dat...");
            LoadExternalBlockFile(Params(), file, nullptr);
            RenameOver(pathBootstrap, pathBootstrapOld);
        } else
        {
            mlog.info("Warning: Could not open bootstrap file %s", pathBootstrap.string());
        }
    }

    for (const std::string &strFile : Args().GetArgs("-loadblock"))
    {
        FILE *file = fsbridge::fopen(strFile, "rb");
        if (file)
        {
            mlog.info("Importing blocks file %s...", strFile);
            LoadExternalBlockFile(Params(), file, nullptr);
        } else
        {
            mlog.info("Warning: Could not open blocks file %s", strFile);
        }
    }

    // scan for better chains in the block chain database, that are not yet connected in the active best chain
    CValidationState state;
    if (!ActivateBestChain(state, Params(), nullptr))
    {
        mlog.info("Failed to connect best block");
        return;
    }

    if (Args().GetArg<bool>("-stopafterblockimport", false))
    {
        mlog.info("Stopping after block import");
        return;
    }
}

void CChainComponent::NotifyHeaderTip()
{
    bool fNotify = false;
    bool fInitialBlockDownload = false;
    static CBlockIndex *pindexHeaderOld = nullptr;
    CBlockIndex *pindexHeader = nullptr;
    {
        LOCK(cs);
        pindexHeader = cIndexManager.GetIndexBestHeader();

        if (pindexHeader != pindexHeaderOld)
        {
            fNotify = true;
            fInitialBlockDownload = IsInitialBlockDownload();
            pindexHeaderOld = pindexHeader;
        }
    }
    // Send block tip changed notifications without cs_main
    if (fNotify)
    {
        uiInterface.NotifyHeaderTip(fInitialBlockDownload, pindexHeader);
    }
}

bool CChainComponent::LoadExternalBlockFile(const CChainParams &chainParams, FILE *fileIn, CDiskBlockPos *dbp)
{
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    try
    {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2 * MAX_BLOCK_SERIALIZED_SIZE, MAX_BLOCK_SERIALIZED_SIZE + 8, SER_DISK,
                             CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof())
        {
            boost::this_thread::interruption_point();

            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try
            {
                // locate a header
                unsigned char buf[CMessageHeader::MESSAGE_START_SIZE];
                blkdat.FindByte(chainParams.MessageStart()[0]);
                nRewind = blkdat.GetPos() + 1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, chainParams.MessageStart(), CMessageHeader::MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > MAX_BLOCK_SERIALIZED_SIZE)
                    continue;
            } catch (const std::exception &)
            {
                // no valid block header found; don't complain
                break;
            }
            try
            {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
                CBlock &block = *pblock;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
                bool bParentNotFound = (cIndexManager.GetBlockIndex(block.hashPrevBlock) == nullptr) ? true : false;
                if (hash != chainParams.GetConsensus().hashGenesisBlock && bParentNotFound)
                {
                    mlog.info("%s: Out of order block %s, parent %s not known\n", __func__,
                              hash.ToString(),
                              block.hashPrevBlock.ToString());
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                if ((cIndexManager.GetBlockIndex(hash) == nullptr) ||
                    (cIndexManager.GetBlockIndex(hash)->nStatus & BLOCK_HAVE_DATA) == 0)
                {
                    LOCK(cs);
                    CValidationState state;
                    if (AcceptBlock(pblock, state, chainParams, nullptr, true, dbp, nullptr))
                        nLoaded++;
                    if (state.IsError())
                        break;
                } else if (hash != chainParams.GetConsensus().hashGenesisBlock &&
                           cIndexManager.GetBlockIndex(hash)->nHeight % 1000 == 0)
                {
                    mlog.info("Block Import: already had block %s at height %d", hash.ToString(),
                              cIndexManager.GetBlockIndex(hash)->nHeight);
                }

                // Activate the genesis block so normal node progress can continue
                if (hash == chainParams.GetConsensus().hashGenesisBlock)
                {
                    CValidationState state;
                    if (!ActivateBestChain(state, chainParams, nullptr))
                    {
                        break;
                    }
                }

                NotifyHeaderTip();

                // Recursively process earlier encountered successors of this block
                std::deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty())
                {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator, std::multimap<uint256, CDiskBlockPos>::iterator> range = mapBlocksUnknownParent.equal_range(
                            head);
                    while (range.first != range.second)
                    {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
                        std::shared_ptr<CBlock> pblockrecursive = std::make_shared<CBlock>();
                        if (ReadBlockFromDisk(*pblockrecursive, it->second, chainParams.GetConsensus()))
                        {
                            mlog.info("%s: Processing out of order child %s of %s\n", __func__,
                                      pblockrecursive->GetHash().ToString(),
                                      head.ToString());
                            LOCK(cs);
                            CValidationState dummy;
                            if (AcceptBlock(pblockrecursive, dummy, chainParams, nullptr, true, &it->second, nullptr))
                            {
                                nLoaded++;
                                queue.push_back(pblockrecursive->GetHash());
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                        NotifyHeaderTip();
                    }
                }
            } catch (const std::exception &e)
            {
                mlog_error("%s: Deserialize or I/O error - %s", __func__, e.what());
            }
        }
    } catch (const std::runtime_error &e)
    {
        AbortNode(std::string("System error: ") + e.what());
    }
    if (nLoaded > 0)
        mlog.info("Loaded %i blocks from external file in %dms", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

/** Store block on disk. If dbp is non-nullptr, the file is known to already reside on disk */
bool CChainComponent::AcceptBlock(const std::shared_ptr<const CBlock> &pblock, CValidationState &state,
                                  const CChainParams &chainparams, CBlockIndex **ppindex, bool fRequested,
                                  const CDiskBlockPos *dbp, bool *fNewBlock)
{
    const CBlock &block = *pblock;

    if (fNewBlock)
        *fNewBlock = false;
    AssertLockHeld(cs);

    CBlockIndex *pindexDummy = nullptr;
    CBlockIndex *&pindex = ppindex ? *ppindex : pindexDummy;

    if (!cIndexManager.AcceptBlockHeader(block, state, chainparams, &pindex))
        return false;

    // Try to process all requested blocks that we don't have, but only
    // process an unrequested block if it's new and has enough work to
    // advance our tip, and isn't too many blocks ahead.
    bool fAlreadyHave = pindex->nStatus & BLOCK_HAVE_DATA;
    CChain &chainActive = cIndexManager.GetChain();
    bool fHasMoreOrSameWork = (chainActive.Tip() ? pindex->nChainWork >= chainActive.Tip()->nChainWork : true);
    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip.  Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks.
    bool fTooFarAhead = (pindex->nHeight > int(chainActive.Height() + MIN_BLOCKS_TO_KEEP));

    // TODO: Decouple this function from the block download logic by removing fRequested
    // This requires some new chain data structure to efficiently look up if a
    // block is in a chain leading to a candidate for best tip, despite not
    // being such a candidate itself.

    // TODO: deal better with return value and error conditions for duplicate
    // and unrequested blocks.
    if (fAlreadyHave)
        return true;
    if (!fRequested)
    {  // If we didn't ask for it:
        if (pindex->nTx != 0)
            return true;    // This is a previously-processed block that was pruned
        if (!fHasMoreOrSameWork)
            return true; // Don't process less-work chains
        if (fTooFarAhead)
            return true;        // Block height is too high

        // Protect against DoS attacks from low-work chains.
        // If our tip is behind, a peer could try to send us
        // low-work blocks on a fake chain that we would never
        // request; don't process these.
        if (pindex->nChainWork < nMinimumChainWork)
            return true;
    }
    if (fNewBlock)
        *fNewBlock = true;

    if (!CheckBlock(block, state, chainparams.GetConsensus(), false, false) ||
        !ContextualCheckBlock(block, state, chainparams.GetConsensus(), pindex->pprev))
    {
        if (state.IsInvalid() && !state.CorruptionPossible())
        {
            pindex->nStatus |= BLOCK_FAILED_VALID;
            cIndexManager.SetDirtyIndex(pindex);
        }
        return error("%s: %s", __func__, FormatStateMessage(state));
    }

    // Header is valid/has work, merkle tree and segwit merkle tree are good...RELAY NOW
    // (but if it does not build on our best tip, let the SendMessages loop relay it)
    if (!IsInitialBlockDownload() && chainActive.Tip() == pindex->pprev)
        NewPoWValidBlock(pindex, pblock);

    int nHeight = pindex->nHeight;

    // Write block to history file
    try
    {
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        if (dbp != nullptr)
            blockPos = *dbp;
        if (!cIndexManager.FindBlockPos(state, blockPos, nBlockSize + 8, nHeight, block.GetBlockTime(), dbp != nullptr))
            return error("AcceptBlock(): FindBlockPos failed");
        if (dbp == nullptr)
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                AbortNode(state, "Failed to write block");
        if (!cIndexManager.ReceivedBlockTransactions(block, state, pindex, blockPos, chainparams.GetConsensus()))
            return error("AcceptBlock(): ReceivedBlockTransactions failed");
    } catch (const std::runtime_error &e)
    {
        return AbortNode(state, std::string("System error: ") + e.what());
    }

    //    if (fCheckForPruning) todo
    //        FlushStateToDisk(chainparams, state, FLUSH_STATE_NONE); // we just allocated more disk space for block files

    return true;
}

bool CChainComponent::LoadCheckPoint()
{
    Checkpoints::CCheckPointDB cCheckPointDB;
    std::map<int, Checkpoints::CCheckData> values;

    if (cCheckPointDB.LoadCheckPoint(values))
    {
        std::map<int, Checkpoints::CCheckData>::iterator it = values.begin();
        while (it != values.end())
        {
            Checkpoints::CCheckData data1 = it->second;
            Params().AddCheckPoint(data1.getHeight(), data1.getHash());
            it++;
        }
    }
    return true;
}

CBlockIndex *CChainComponent::FindForkInGlobalIndex(const CChain &chain, const CBlockLocator &locator)
{
    return cIndexManager.FindForkInGlobalIndex(chain, locator);
}

log4cpp::Category &CChainComponent::getLog()
{
    return mlog;
}

bool CChainComponent::VerifyDB(const CChainParams &chainparams, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    uiInterface.ShowProgress(_("Verifying blocks..."), 0);

    CChain &chainActive = cIndexManager.GetChain();
    if (chainActive.Tip() == nullptr || chainActive.Tip()->pprev == nullptr)
        return true;

    // Verify blocks in the best chain
    if (nCheckDepth <= 0 || nCheckDepth > chainActive.Height())
        nCheckDepth = chainActive.Height();
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex *pindexState = chainActive.Tip();
    CBlockIndex *pindexFailure = nullptr;
    int nGoodTransactions = 0;
    CValidationState state;
    int reportDone = 0;
    LogPrintf("[0%%]...");
    for (CBlockIndex *pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev)
    {
        boost::this_thread::interruption_point();
        int percentageDone = std::max(1, std::min(99, (int)(((double)(chainActive.Height() - pindex->nHeight)) /
                                                            (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100))));
        if (reportDone < percentageDone / 10)
        {
            // report every 10% step
            LogPrintf("[%d%%]...", percentageDone);
            reportDone = percentageDone / 10;
        }
        uiInterface.ShowProgress(_("Verifying blocks..."), percentageDone);
        if (pindex->nHeight < chainActive.Height() - nCheckDepth)
            break;
        if (fPruneMode && !(pindex->nStatus & BLOCK_HAVE_DATA))
        {
            // If pruning, only go back as far as we have data.
            LogPrintf("VerifyDB(): block verification stopping at height %d (pruning, no data)\n", pindex->nHeight);
            break;
        }
        CBlock block;
        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus()))
            return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight,
                         pindex->GetBlockHash().ToString());
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state, chainparams.GetConsensus(), false, false))
            return error("%s: *** found bad block at %d, hash=%s (%s)\n", __func__,
                         pindex->nHeight, pindex->GetBlockHash().ToString(), FormatStateMessage(state));
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex)
        {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull())
            {
                if (!UndoReadFromDisk(undo, pos, pindex->pprev->GetBlockHash()))
                    return error("VerifyDB(): *** found bad undo data at %d, hash=%s\n", pindex->nHeight,
                                 pindex->GetBlockHash().ToString());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState &&
            (coins.DynamicMemoryUsage() + GetCoinsTip()->DynamicMemoryUsage()) <= iCoinCacheUsage)
        {
            assert(coins.GetBestBlock() == pindex->GetBlockHash());
            DisconnectResult res = cViewManager.DisconnectBlock(block, pindex, coins);
            if (res == DISCONNECT_FAILED)
            {
                return error("VerifyDB(): *** irrecoverable inconsistency in block data at %d, hash=%s",
                             pindex->nHeight, pindex->GetBlockHash().ToString());
            }
            pindexState = pindex->pprev;
            if (res == DISCONNECT_UNCLEAN)
            {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else
            {
                nGoodTransactions += block.vtx.size();
            }
        }
        if (app().ShutdownRequested())
            return true;
    }
    if (pindexFailure)
        return error(
                "VerifyDB(): *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n",
                chainActive.Height() - pindexFailure->nHeight + 1, nGoodTransactions);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4)
    {
        CBlockIndex *pindex = pindexState;
        while (pindex != chainActive.Tip())
        {
            boost::this_thread::interruption_point();
            uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, 100 - (int)(((double)(
                    chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * 50))));
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus()))
                return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight,
                             pindex->GetBlockHash().ToString());
            if (!ConnectBlock(block, state, pindex, coins, chainparams))
                return error("VerifyDB(): *** found unconnectable block at %d, hash=%s", pindex->nHeight,
                             pindex->GetBlockHash().ToString());
        }
    }

    LogPrintf("[DONE].\n");
    LogPrintf("No coin database inconsistencies in last %i blocks (%i transactions)\n",
              chainActive.Height() - pindexState->nHeight, nGoodTransactions);

    uiInterface.ShowProgress("", 100);

    return true;
}

// Exposed wrapper for AcceptBlockHeader
bool CChainComponent::ProcessNewBlockHeaders(const std::vector<CBlockHeader> &headers, CValidationState &state,
                                             const CChainParams &chainparams, const CBlockIndex **ppindex,
                                             CBlockHeader *first_invalid)
{
    if (first_invalid != nullptr)
        first_invalid->SetNull();
    {
        LOCK(cs_main);
        for (const CBlockHeader &header : headers)
        {
            CBlockIndex *pindex = nullptr; // Use a temp pindex instead of ppindex to avoid a const_cast
            if (!cIndexManager.AcceptBlockHeader(header, state, chainparams, &pindex))
            {
                if (first_invalid)
                    *first_invalid = header;
                return false;
            }
            if (ppindex)
            {
                *ppindex = pindex;
            }
        }
    }
    NotifyHeaderTip();
    return true;
}

bool CChainComponent::ProcessNewBlock(const CChainParams &chainparams, const std::shared_ptr<const CBlock> pblock,
                                      bool fForceProcessing,
                                      bool *fNewBlock)
{
    {
        CBlockIndex *pindex = nullptr;
        if (fNewBlock)
            *fNewBlock = false;
        CValidationState state;
        // Ensure that CheckBlock() passes before calling AcceptBlock, as
        // belt-and-suspenders.
        bool ret = CheckBlock(*pblock, state, chainparams.GetConsensus(), false, false);

        LOCK(cs_main);

        if (ret)
        {
            // Store to disk
            ret = AcceptBlock(pblock, state, chainparams, &pindex, fForceProcessing, nullptr, fNewBlock);
        }
        cIndexManager.CheckBlockIndex(chainparams.GetConsensus());
        if (!ret)
        {
            GetMainSignals().BlockChecked(*pblock, state);
            return error("%s: AcceptBlock FAILED", __func__);
        }
    }

    NotifyHeaderTip();

    CValidationState state; // Only used to report errors, not invalidity - ignore it
    if (!ActivateBestChain(state, chainparams, pblock))
        return error("%s: ActivateBestChain failed", __func__);

    return true;
}

bool CChainComponent::TestBlockValidity(CValidationState &state, const CChainParams &chainparams, const CBlock &block,
                                        CBlockIndex *pindexPrev, bool fCheckPOW, bool fCheckMerkleRoot)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev && pindexPrev == cIndexManager.GetChain().Tip());
    CCoinsViewCache viewNew(cViewManager.GetCoinsTip());
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!cIndexManager.ContextualCheckBlockHeader(block, state, chainparams, pindexPrev, GetAdjustedTime()))
        return error("%s: Consensus::ContextualCheckBlockHeader: %s", __func__, FormatStateMessage(state));
    if (!CheckBlock(block, state, chainparams.GetConsensus(), fCheckPOW, fCheckMerkleRoot))
        return error("%s: Consensus::CheckBlock: %s", __func__, FormatStateMessage(state));
    if (!ContextualCheckBlock(block, state, chainparams.GetConsensus(), pindexPrev))
        return error("%s: Consensus::ContextualCheckBlock: %s", __func__, FormatStateMessage(state));
    if (!ConnectBlock(block, state, &indexDummy, viewNew, chainparams, true))
        return false;
    assert(state.IsValid());

    return true;
}

/* This function is called from the RPC code for pruneblockchain */
void CChainComponent::PruneBlockFilesManual(int nManualPruneHeight)
{
    //    CValidationState state;
    //    const CChainParams &chainparams = Params();
    //    FlushStateToDisk(chainparams, state, FLUSH_STATE_NONE, nManualPruneHeight);
}

bool CChainComponent::PreciousBlock(CValidationState &state, const CChainParams &params, CBlockIndex *pindex)
{
    cIndexManager.PreciousBlock(state, params, pindex);

    GET_CHAIN_INTERFACE(ifChainObj);
    return ifChainObj->ActivateBestChain(state, params, nullptr);
}

bool CChainComponent::ResetBlockFailureFlags(CBlockIndex *pindex)
{
    return cIndexManager.ResetBlockFailureFlags(pindex);
}

int CChainComponent::GetSpendHeight(const CCoinsViewCache &inputs)
{
    LOCK(cs);
    CBlockIndex *pindexPrev = cIndexManager.GetBlockIndex(inputs.GetBestBlock());
    if (pindexPrev == nullptr)
    {
        return 1;
    }
    return pindexPrev->nHeight + 1;
}

void CChainComponent::UpdateCoins(const CTransaction &tx, CCoinsViewCache &inputs, int nHeight)
{
    cViewManager.UpdateCoins(tx, inputs, nHeight);
}

CAmount CChainComponent::GetBlockSubsidy(int nHeight)
{
    if (IsSBTCForkHeight(Params().GetConsensus(), nHeight))
    {
        return 210000 * COIN;
    }
    int halvings = nHeight / Params().GetConsensus().nSubsidyHalvingInterval;
    // Force block reward to zero when right shift is undefined.
    if (halvings >= 64)
        return 0;

    CAmount nSubsidy = 50 * COIN;
    // Subsidy is cut in half every 210,000 blocks which will occur approximately every 4 years.
    nSubsidy >>= halvings;
    return nSubsidy;
}

CCoinsView *CChainComponent::GetCoinViewDB()
{
    return cViewManager.GetCoinViewDB();
}

CCoinsViewCache *CChainComponent::GetCoinsTip()
{
    return cViewManager.GetCoinsTip();
}

CBlockTreeDB *CChainComponent::GetBlockTreeDB()
{
    return cIndexManager.GetBlockTreeDB();
}

CBlockIndex *CChainComponent::GetIndexBestHeader()
{
    CBlockIndex *bestHeader = cIndexManager.GetIndexBestHeader();
    if (bestHeader == nullptr)
    {
        bestHeader = Tip();
    }
    return bestHeader;
}