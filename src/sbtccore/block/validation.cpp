// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validation.h"

#include "utils/arith_uint256.h"
#include "chaincontrol/chain.h"
#include "config/chainparams.h"
#include "chaincontrol/checkpoints.h"
#include "interface/ichaincomponent.h"
#include "sbtccore/checkqueue.h"
#include "config/consensus.h"
#include "merkle.h"
#include "chaincontrol/validation.h"
#include "sbtccore/cuckoocache.h"
#include "fs.h"
#include "hash.h"
#include "framework/init.h"
#include "wallet/fees.h"
#include "sbtccore/transaction/policy.h"
#include "wallet/rbf.h"
#include "miner/pow.h"
#include "block.h"
#include "transaction/transaction.h"
#include "random.h"
#include "reverse_iterator.h"
#include "script/script.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "timedata.h"
#include "tinyformat.h"
#include "transaction/txdb.h"
#include "mempool/txmempool.h"
#include "framework/ui_interface.h"
#include "undo.h"
#include "utils/util.h"
#include "utils/utilmoneystr.h"
#include "utils/utilstrencodings.h"
#include "framework/validationinterface.h"
#include "framework/versionbits.h"
#include "framework/warnings.h"
#include "framework/base.hpp"
#include "chaincontrol/blockfilemanager.h"

#include <atomic>
#include <sstream>

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/thread.hpp>

#if defined(NDEBUG)
# error "Bitcoin cannot be compiled without assertions."
#endif

/**
 * Global state
 */

CCriticalSection cs_main;

BlockMap mapBlockIndex;
CChain chainActive;
CBlockIndex *pindexBestHeader = nullptr;
CWaitableCriticalSection csBestBlock;
CConditionVariable cvBlockChange;
int nScriptCheckThreads = 0;
std::atomic_bool fImporting(false);
bool fReindex = false;
bool fTxIndex = false;
bool fHavePruned = false;
bool fPruneMode = false;
bool fIsBareMultisigStd = DEFAULT_PERMIT_BAREMULTISIG;
bool fRequireStandard = true;
bool fCheckBlockIndex = false;
bool fCheckpointsEnabled = DEFAULT_CHECKPOINTS_ENABLED;
size_t nCoinCacheUsage = 5000 * 300;
uint64_t nPruneTarget = 0;
int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE;
bool fEnableReplacement = DEFAULT_ENABLE_REPLACEMENT;

uint256 hashAssumeValid;
arith_uint256 nMinimumChainWork;

CFeeRate minRelayTxFee = CFeeRate(DEFAULT_MIN_RELAY_TX_FEE);
CAmount maxTxFee = DEFAULT_TRANSACTION_MAXFEE;

CBlockPolicyEstimator feeEstimator;
CTxMemPool mempool(&feeEstimator);

/** Constant stuff for coinbase transactions we create: */
CScript COINBASE_FLAGS;

const std::string strMessageMagic = "Bitcoin Signed Message:\n";

// Internal stuff
namespace
{

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

    CBlockIndex *pindexBestInvalid;

    /**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
     * missing the data for the block.
     */
    std::set<CBlockIndex *, CBlockIndexWorkComparator> setBlockIndexCandidates;
    /** All pairs A->B, where A (or one of its ancestors) misses transactions, but B has transactions.
     * Pruned nodes may have entries where B is missing data.
     */
    std::multimap<CBlockIndex *, CBlockIndex *> mapBlocksUnlinked;

    CCriticalSection cs_LastBlockFile;
    std::vector<CBlockFileInfo> vinfoBlockFile;
    int nLastBlockFile = 0;
    /** Global flag to indicate we should check to see if there are
     *  block/undo files that should be deleted.  Set on startup
     *  or if we allocate more file space when we're in prune mode
     */
    bool fCheckForPruning = false;

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

    /** In order to efficiently track invalidity of headers, we keep the set of
      * blocks which we tried to connect and found to be invalid here (ie which
      * were set to BLOCK_FAILED_VALID since the last restart). We can then
      * walk this set and check if a new header is a descendant of something in
      * this set, preventing us from having to walk mapBlockIndex when we try
      * to connect a bad block and fail.
      *
      * While this is more complicated than marking everything which descends
      * from an invalid block as invalid at the time we discover it to be
      * invalid, doing so would require walking all of mapBlockIndex to find all
      * descendants. Since this case should be very rare, keeping track of all
      * BLOCK_FAILED_VALID blocks in a set should be just fine and work just as
      * well.
      *
      * Because we alreardy walk mapBlockIndex in height-order at startup, we go
      * ahead and mark descendants of invalid blocks as FAILED_CHILD at that time,
      * instead of putting things in this set.
      */
    std::set<CBlockIndex *> g_failed_blocks;

    /** Dirty block index entries. */
    std::set<CBlockIndex *> setDirtyBlockIndex;

    /** Dirty block file entries. */
    std::set<int> setDirtyFileInfo;
} // anon namespace

CCoinsViewDB *pcoinsdbview = nullptr;
CCoinsViewCache *pcoinsTip = nullptr;
CBlockTreeDB *pblocktree = nullptr;

enum FlushStateMode
{
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};

bool CheckFinalTx(const CTransaction &tx, int flags)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses chainActive.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than chainActive.Height().
    const int nBlockHeight = chainActive.Height() + 1;

    // BIP113 will require that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST)
                               ? chainActive.Tip()->GetMedianTimePast()
                               : GetAdjustedTime();

    //    GET_VERIFY_INTERFACE(ifVerifyObj);
    return tx.IsFinalTx(nBlockHeight, nBlockTime);
}

bool TestLockPointValidity(const LockPoints *lp)
{
    AssertLockHeld(cs_main);
    assert(lp);
    // If there are relative lock times then the maxInputBlock will be set
    // If there are no relative lock times, the LockPoints don't depend on the chain
    if (lp->maxInputBlock)
    {
        // Check whether chainActive is an extension of the block at which the LockPoints
        // calculation was valid.  If not LockPoints are no longer valid
        if (!chainActive.Contains(lp->maxInputBlock))
        {
            return false;
        }
    }

    // LockPoints still valid
    return true;
}

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state)
{
    return strprintf("%s%s (code %i)",
                     state.GetRejectReason(),
                     state.GetDebugMessage().empty() ? "" : ", " + state.GetDebugMessage(),
                     state.GetRejectCode());
}

/** Return transaction in txOut, and if it was found inside a block, its hash is placed in hashBlock */
bool GetTransaction(const uint256 &hash, CTransactionRef &txOut, const Consensus::Params &consensusParams,
                    uint256 &hashBlock, bool fAllowSlow)
{
    CBlockIndex *pindexSlow = nullptr;

    LOCK(cs_main);

    CTransactionRef ptx = mempool.get(hash);
    if (ptx)
    {
        txOut = ptx;
        return true;
    }

    if (fTxIndex)
    {
        CDiskTxPos postx;
        if (pblocktree->ReadTxIndex(hash, postx))
        {
            CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
            if (file.IsNull())
                return error("%s: OpenBlockFile failed", __func__);
            CBlockHeader header;
            try
            {
                file >> header;
                fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
                file >> txOut;
            } catch (const std::exception &e)
            {
                return error("%s: Deserialize or I/O error - %s", __func__, e.what());
            }
            hashBlock = header.GetHash();
            if (txOut->GetHash() != hash)
                return error("%s: txid mismatch", __func__);
            return true;
        }
    }

    if (fAllowSlow)
    { // use coin database to locate block that contains transaction, and scan it
        const Coin &coin = AccessByTxid(*pcoinsTip, hash);
        if (!coin.IsSpent())
            pindexSlow = chainActive[coin.nHeight];
    }

    if (pindexSlow)
    {
        CBlock block;
        if (ReadBlockFromDisk(block, pindexSlow, consensusParams))
        {
            for (const auto &tx : block.vtx)
            {
                if (tx->GetHash() == hash)
                {
                    txOut = tx;
                    hashBlock = pindexSlow->GetBlockHash();
                    return true;
                }
            }
        }
    }

    return false;
}

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params &consensusParams)
{
    if (IsSBTCForkHeight(consensusParams, nHeight))
    {
        return 210000 * COIN;
    }
    int halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
    // Force block reward to zero when right shift is undefined.
    if (halvings >= 64)
        return 0;

    CAmount nSubsidy = 50 * COIN;
    // Subsidy is cut in half every 210,000 blocks which will occur approximately every 4 years.
    nSubsidy >>= halvings;
    return nSubsidy;
}

CBlockIndex *pindexBestForkTip = nullptr, *pindexBestForkBase = nullptr;

void AlertNotify(const std::string &strMessage)
{
    uiInterface.NotifyAlertChanged();
    std::string strCmd = gArgs.GetArg<std::string>("-alertnotify", "");
    if (strCmd.empty())
        return;

    // Alert text should be plain ascii coming from a trusted source, but to
    // be safe we first strip anything not in safeChars, then add single quotes around
    // the whole string before passing it to the shell:
    std::string singleQuote("'");
    std::string safeStatus = SanitizeString(strMessage);
    safeStatus = singleQuote + safeStatus + singleQuote;
    boost::replace_all(strCmd, "%s", safeStatus);

    boost::thread t(runCommand, strCmd); // thread runs free
}

void UpdateCoins(const CTransaction &tx, CCoinsViewCache &inputs, CTxUndo &txundo, int nHeight)
{
    // mark inputs spent
    if (!tx.IsCoinBase())
    {
        txundo.vprevout.reserve(tx.vin.size());
        for (const CTxIn &txin : tx.vin)
        {
            txundo.vprevout.emplace_back();
            bool is_spent = inputs.SpendCoin(txin.prevout, &txundo.vprevout.back());
            assert(is_spent);
        }
    }
    // add outputs
    AddCoins(inputs, tx, nHeight);
}

void UpdateCoins(const CTransaction &tx, CCoinsViewCache &inputs, int nHeight)
{
    CTxUndo txundo;
    UpdateCoins(tx, inputs, txundo, nHeight);
}

bool CScriptCheck::operator()()
{
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    const CScriptWitness *witness = &ptxTo->vin[nIn].scriptWitness;
    return VerifyScript(scriptSig, scriptPubKey, witness, nFlags,
                        CachingTransactionSignatureChecker(ptxTo, nIn, amount, cacheStore, *txdata), &error);
}

int GetSpendHeight(const CCoinsViewCache &inputs)
{
    LOCK(cs_main);
    CBlockIndex *pindexPrev = mapBlockIndex.find(inputs.GetBestBlock())->second;
    return pindexPrev->nHeight + 1;
}


static CuckooCache::cache<uint256, SignatureCacheHasher> scriptExecutionCache;
static uint256 scriptExecutionCacheNonce(GetRandHash());

void InitScriptExecutionCache()
{
    // nMaxCacheSize is unsigned. If -maxsigcachesize is set to zero,
    // setup_bytes creates the minimum possible cache (2 elements).
    size_t nMaxCacheSize = std::min(
            std::max((int64_t)0, int64_t(gArgs.GetArg<uint32_t>("-maxsigcachesize", DEFAULT_MAX_SIG_CACHE_SIZE) / 2)),
            MAX_MAX_SIG_CACHE_SIZE) * ((size_t)1 << 20);
    size_t nElems = scriptExecutionCache.setup_bytes(nMaxCacheSize);
    LogPrintf("Using %zu MiB out of %zu/2 requested for script execution cache, able to store %zu elements\n",
              (nElems * sizeof(uint256)) >> 20, (nMaxCacheSize * 2) >> 20, nElems);
}

///**
// * Check whether all inputs of this transaction are valid (no double spends, scripts & sigs, amounts)
// * This does not modify the UTXO set.
// *
// * If pvChecks is not nullptr, script checks are pushed onto it instead of being performed inline. Any
// * script checks which are not necessary (eg due to script execution cache hits) are, obviously,
// * not pushed onto pvChecks/run.
// *
// * Setting cacheSigStore/cacheFullScriptStore to false will remove elements from the corresponding cache
// * which are matched. This is useful for checking blocks where we will likely never need the cache
// * entry again.
// *
// * Non-static (and re-declared) in src/test/txvalidationcache_tests.cpp
// */
//bool CheckInputs(const CTransaction &tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks,
//                 unsigned int flags, bool cacheSigStore, bool cacheFullScriptStore, PrecomputedTransactionData &txdata,
//                 std::vector<CScriptCheck> *pvChecks)
//{
//    if (!tx.IsCoinBase())
//    {
//        GET_VERIFY_INTERFACE(ifVerifyObj);
//        if (!ifVerifyObj->CheckTxInputs(tx, state, inputs, GetSpendHeight(inputs)))
//            return false;
//
//        if (pvChecks)
//            pvChecks->reserve(tx.vin.size());
//
//        // The first loop above does all the inexpensive checks.
//        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
//        // Helps prevent CPU exhaustion attacks.
//
//        // Skip script verification when connecting blocks under the
//        // assumevalid block. Assuming the assumevalid block is valid this
//        // is safe because block merkle hashes are still computed and checked,
//        // Of course, if an assumed valid block is invalid due to false scriptSigs
//        // this optimization would allow an invalid chain to be accepted.
//        if (fScriptChecks)
//        {
//            // First check if script executions have been cached with the same
//            // flags. Note that this assumes that the inputs provided are
//            // correct (ie that the transaction hash which is in tx's prevouts
//            // properly commits to the scriptPubKey in the inputs view of that
//            // transaction).
//            uint256 hashCacheEntry;
//            // We only use the first 19 bytes of nonce to avoid a second SHA
//            // round - giving us 19 + 32 + 4 = 55 bytes (+ 8 + 1 = 64)
//            static_assert(55 - sizeof(flags) - 32 >= 128 / 8,
//                          "Want at least 128 bits of nonce for script execution cache");
//            CSHA256().Write(scriptExecutionCacheNonce.begin(), 55 - sizeof(flags) - 32).Write(
//                    tx.GetWitnessHash().begin(), 32).Write((unsigned char *)&flags, sizeof(flags)).Finalize(
//                    hashCacheEntry.begin());
//            AssertLockHeld(cs_main); //TODO: Remove this requirement by making CuckooCache not require external locks
//            if (scriptExecutionCache.contains(hashCacheEntry, !cacheFullScriptStore))
//            {
//                return true;
//            }
//
//            for (unsigned int i = 0; i < tx.vin.size(); i++)
//            {
//                const COutPoint &prevout = tx.vin[i].prevout;
//                const Coin &coin = inputs.AccessCoin(prevout);
//                assert(!coin.IsSpent());
//
//                // We very carefully only pass in things to CScriptCheck which
//                // are clearly committed to by tx' witness hash. This provides
//                // a sanity check that our caching is not introducing consensus
//                // failures through additional data in, eg, the coins being
//                // spent being checked as a part of CScriptCheck.
//                const CScript &scriptPubKey = coin.out.scriptPubKey;
//                const CAmount amount = coin.out.nValue;
//
//                // Verify signature
//                CScriptCheck check(scriptPubKey, amount, tx, i, flags, cacheSigStore, &txdata);
//                if (pvChecks)
//                {
//                    pvChecks->push_back(CScriptCheck());
//                    check.swap(pvChecks->back());
//                } else if (!check())
//                {
//                    if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS)
//                    {
//                        // Check whether the failure was caused by a
//                        // non-mandatory script verification check, such as
//                        // non-standard DER encodings or non-null dummy
//                        // arguments; if so, don't trigger DoS protection to
//                        // avoid splitting the network between upgraded and
//                        // non-upgraded nodes.
//                        CScriptCheck check2(scriptPubKey, amount, tx, i,
//                                            flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, cacheSigStore, &txdata);
//                        if (check2())
//                            return state.Invalid(false, REJECT_NONSTANDARD,
//                                                 strprintf("non-mandatory-script-verify-flag (%s)",
//                                                           ScriptErrorString(check.GetScriptError())));
//                    }
//                    // Failures of other flags indicate a transaction that is
//                    // invalid in new blocks, e.g. an invalid P2SH. We DoS ban
//                    // such nodes as they are not following the protocol. That
//                    // said during an upgrade careful thought should be taken
//                    // as to the correct behavior - we may want to continue
//                    // peering with non-upgraded nodes even after soft-fork
//                    // super-majority signaling has occurred.
//                    return state.DoS(100, false, REJECT_INVALID, strprintf("mandatory-script-verify-flag-failed (%s)",
//                                                                           ScriptErrorString(check.GetScriptError())));
//                }
//            }
//
//            if (cacheFullScriptStore && !pvChecks)
//            {
//                // We executed all of the provided scripts, and were told to
//                // cache the result. Do so now.
//                scriptExecutionCache.insert(hashCacheEntry);
//            }
//        }
//    }
//
//    return true;
//}

//namespace
//{

bool UndoWriteToDisk(const CBlockUndo &blockundo, CDiskBlockPos &pos, const uint256 &hashBlock,
                     const CMessageHeader::MessageStartChars &messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenUndoFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s: OpenUndoFile failed", __func__);

    // Write index header
    unsigned int nSize = GetSerializeSize(fileout, blockundo);
    fileout << FLATDATA(messageStart) << nSize;

    // Write undo data
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("%s: ftell failed", __func__);
    pos.nPos = (unsigned int)fileOutPos;
    fileout << blockundo;

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    fileout << hasher.GetHash();

    return true;
}

/** Abort with a message */
bool AbortNode(const std::string &strMessage, const std::string &userMessage)
{
    SetMiscWarning(strMessage);
    LogPrintf("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
            userMessage.empty() ? _("Error: A fatal internal error occurred, see debug.log for details")
                                : userMessage,
            "", CClientUIInterface::MSG_ERROR);
    StartShutdown();
    return false;
}

bool AbortNode(CValidationState &state, const std::string &strMessage, const std::string &userMessage)
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

//} // namespace

enum DisconnectResult
{
    DISCONNECT_OK,      // All good.
    DISCONNECT_UNCLEAN, // Rolled back, but UTXO set was inconsistent with block.
    DISCONNECT_FAILED   // Something else went wrong.
};

/**
 * Restore the UTXO in a Coin at a given COutPoint
 * @param undo The Coin to be restored.
 * @param view The coins view to which to apply the changes.
 * @param out The out point that corresponds to the tx input.
 * @return A DisconnectResult as an int
 */
int ApplyTxInUndo(Coin &&undo, CCoinsViewCache &view, const COutPoint &out)
{
    bool fClean = true;

    if (view.HaveCoin(out))
        fClean = false; // overwriting transaction output

    if (undo.nHeight == 0)
    {
        // Missing undo metadata (height and coinbase). Older versions included this
        // information only in undo records for the last spend of a transactions'
        // outputs. This implies that it must be present for some other output of the same tx.
        const Coin &alternate = AccessByTxid(view, out.hash);
        if (!alternate.IsSpent())
        {
            undo.nHeight = alternate.nHeight;
            undo.fCoinBase = alternate.fCoinBase;
        } else
        {
            return DISCONNECT_FAILED; // adding output for transaction without known metadata
        }
    }
    // The potential_overwrite parameter to AddCoin is only allowed to be false if we know for
    // sure that the coin did not already exist in the cache. As we have queried for that above
    // using HaveCoin, we don't need to guess. When fClean is false, a coin already existed and
    // it is an overwrite.
    view.AddCoin(out, std::move(undo), !fClean);

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

void static FlushBlockFile(bool fFinalize = false)
{
    LOCK(cs_LastBlockFile);

    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE *fileOld = OpenBlockFile(posOld);
    if (fileOld)
    {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld)
    {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

static CCheckQueue<CScriptCheck> scriptcheckqueue(128);

void ThreadScriptCheck()
{
    RenameThread("bitcoin-scriptch");
    scriptcheckqueue.Thread();
}

// Protected by cs_main
VersionBitsCache versionbitscache;

int32_t ComputeBlockVersion(const CBlockIndex *pindexPrev, const Consensus::Params &params)
{
    LOCK(cs_main);
    int32_t nVersion = VERSIONBITS_TOP_BITS;

    for (int i = 0; i < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; i++)
    {
        ThresholdState state = VersionBitsState(pindexPrev, params, (Consensus::DeploymentPos)i, versionbitscache);
        if (state == THRESHOLD_LOCKED_IN || state == THRESHOLD_STARTED)
        {
            nVersion |= VersionBitsMask(params, (Consensus::DeploymentPos)i);
        }
    }

    return nVersion;
}

/**
 * Threshold condition checker that triggers when unknown versionbits are seen on the network.
 */
class WarningBitsConditionChecker : public AbstractThresholdConditionChecker
{
private:
    int bit;

public:
    WarningBitsConditionChecker(int bitIn) : bit(bitIn)
    {
    }

    int64_t BeginTime(const Consensus::Params &params) const override
    {
        return 0;
    }

    int64_t EndTime(const Consensus::Params &params) const override
    {
        return std::numeric_limits<int64_t>::max();
    }

    int Period(const Consensus::Params &params) const override
    {
        return params.nMinerConfirmationWindow;
    }

    int Threshold(const Consensus::Params &params) const override
    {
        return params.nRuleChangeActivationThreshold;
    }

    bool Condition(const CBlockIndex *pindex, const Consensus::Params &params) const override
    {
        return ((pindex->nVersion & VERSIONBITS_TOP_MASK) == VERSIONBITS_TOP_BITS) &&
               ((pindex->nVersion >> bit) & 1) != 0 &&
               ((ComputeBlockVersion(pindex->pprev, params) >> bit) & 1) == 0;
    }
};

// Protected by cs_main
static ThresholdConditionCache warningcache[VERSIONBITS_NUM_BITS];

struct PerBlockConnectTrace
{
    CBlockIndex *pindex = nullptr;
    std::shared_ptr<const CBlock> pblock;
    std::shared_ptr<std::vector<CTransactionRef>> conflictedTxs;

    PerBlockConnectTrace() : conflictedTxs(std::make_shared<std::vector<CTransactionRef>>())
    {
    }
};

/**
 * Used to track blocks whose transactions were applied to the UTXO state as a
 * part of a single ActivateBestChainStep call.
 *
 * This class also tracks transactions that are removed from the mempool as
 * conflicts (per block) and can be used to pass all those transactions
 * through SyncTransaction.
 *
 * This class assumes (and asserts) that the conflicted transactions for a given
 * block are added via mempool callbacks prior to the BlockConnected() associated
 * with those transactions. If any transactions are marked conflicted, it is
 * assumed that an associated block will always be added.
 *
 * This class is single-use, once you call GetBlocksConnected() you have to throw
 * it away and make a new one.
 */
class ConnectTrace
{
private:
    std::vector<PerBlockConnectTrace> blocksConnected;
    CTxMemPool &pool;

public:
    ConnectTrace(CTxMemPool &_pool) : blocksConnected(1), pool(_pool)
    {
        pool.NotifyEntryRemoved.connect(boost::bind(&ConnectTrace::NotifyEntryRemoved, this, _1, _2));
    }

    ~ConnectTrace()
    {
        pool.NotifyEntryRemoved.disconnect(boost::bind(&ConnectTrace::NotifyEntryRemoved, this, _1, _2));
    }

    void BlockConnected(CBlockIndex *pindex, std::shared_ptr<const CBlock> pblock)
    {
        assert(!blocksConnected.back().pindex);
        assert(pindex);
        assert(pblock);
        blocksConnected.back().pindex = pindex;
        blocksConnected.back().pblock = std::move(pblock);
        blocksConnected.emplace_back();
    }

    std::vector<PerBlockConnectTrace> &GetBlocksConnected()
    {
        // We always keep one extra block at the end of our list because
        // blocks are added after all the conflicted transactions have
        // been filled in. Thus, the last entry should always be an empty
        // one waiting for the transactions from the next block. We pop
        // the last entry here to make sure the list we return is sane.
        assert(!blocksConnected.back().pindex);
        assert(blocksConnected.back().conflictedTxs->empty());
        blocksConnected.pop_back();
        return blocksConnected;
    }

    void NotifyEntryRemoved(CTransactionRef txRemoved, MemPoolRemovalReason reason)
    {
        assert(!blocksConnected.back().pindex);
        if (reason == MemPoolRemovalReason::CONFLICT)
        {
            blocksConnected.back().conflictedTxs->emplace_back(std::move(txRemoved));
        }
    }
};

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
static void PruneBlockIndexCandidates()
{
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    std::set<CBlockIndex *, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, chainActive.Tip()))
    {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

bool IsAgainstCheckPoint(const CChainParams &chainparams, const CBlockIndex *pindex)
{

    auto lastpioint = Checkpoints::GetLastCheckPointBlockIndex(chainparams.Checkpoints());

    if (lastpioint == nullptr)
    {
        return false;
    }

    if (pindex->nHeight >= lastpioint->nHeight)
    {

        if (pindex->GetAncestor(lastpioint->nHeight)->GetBlockHash() == lastpioint->GetBlockHash())
        {
            return false;
        }

    } else
    {
        if (lastpioint->GetAncestor(pindex->nHeight)->GetBlockHash() == pindex->GetBlockHash())
        {
            return false;
        }
    }
    return true;
}


bool IsAgainstCheckPoint(const CChainParams &chainparams, const int &nHeight, const uint256 &hash)
{
    const auto tPoint = chainparams.Checkpoints();
    auto test = tPoint.mapCheckpoints.find(nHeight);
    if (test != tPoint.mapCheckpoints.end())
    {
        if (test->second != hash)
            return true;
    }
    return false;
}

bool PreciousBlock(CValidationState &state, const CChainParams &params, CBlockIndex *pindex)
{
    {
        LOCK(cs_main);
        if (pindex->nChainWork < chainActive.Tip()->nChainWork)
        {
            // Nothing to do, this block is not at the tip.
            return true;
        }
        if (chainActive.Tip()->nChainWork > nLastPreciousChainwork)
        {
            // The chain has been extended since the last call, reset the counter.
            nBlockReverseSequenceId = -1;
        }
        nLastPreciousChainwork = chainActive.Tip()->nChainWork;
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

    GET_CHAIN_INTERFACE(ifChainObj);
    return ifChainObj->ActivateBestChain(state, params, nullptr);
}

bool ResetBlockFailureFlags(CBlockIndex *pindex)
{
    AssertLockHeld(cs_main);

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end())
    {
        if (!it->second->IsValid() && it->second->GetAncestor(nHeight) == pindex)
        {
            it->second->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(it->second);
            if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx &&
                setBlockIndexCandidates.value_comp()(chainActive.Tip(), it->second))
            {
                setBlockIndexCandidates.insert(it->second);
            }
            if (it->second == pindexBestInvalid)
            {
                // Reset invalid block marker if it was pointing to one of those.
                pindexBestInvalid = nullptr;
            }
            g_failed_blocks.erase(it->second);
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

static CBlockIndex *AddToBlockIndex(const CBlockHeader &block)
{
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end())
        return it->second;

    // Construct new block index object
    CBlockIndex *pindexNew = new CBlockIndex(block);
    assert(pindexNew);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = mapBlockIndex.find(block.hashPrevBlock);
    if (miPrev != mapBlockIndex.end())
    {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();
    }
    pindexNew->nTimeMax = (pindexNew->pprev ? std::max(pindexNew->pprev->nTimeMax, pindexNew->nTime)
                                            : pindexNew->nTime);
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);
    if (pindexBestHeader == nullptr || pindexBestHeader->nChainWork < pindexNew->nChainWork)
        pindexBestHeader = pindexNew;

    setDirtyBlockIndex.insert(pindexNew);

    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
static bool ReceivedBlockTransactions(const CBlock &block, CValidationState &state, CBlockIndex *pindexNew,
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
                LOCK(cs_nBlockSequenceId);
                pindex->nSequenceId = nBlockSequenceId++;
            }
            if (chainActive.Tip() == nullptr || !setBlockIndexCandidates.value_comp()(pindex, chainActive.Tip()))
            {
                setBlockIndexCandidates.insert(pindex);
            }
            std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator, std::multimap<CBlockIndex *, CBlockIndex *>::iterator> range = mapBlocksUnlinked.equal_range(
                    pindex);
            while (range.first != range.second)
            {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator it = range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    } else
    {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE))
        {
            mapBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }

    return true;
}

static bool
FindBlockPos(CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime,
             bool fKnown = false)
{
    LOCK(cs_LastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : nLastBlockFile;
    if (vinfoBlockFile.size() <= nFile)
    {
        vinfoBlockFile.resize(nFile + 1);
    }

    if (!fKnown)
    {
        while (vinfoBlockFile[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE)
        {
            nFile++;
            if (vinfoBlockFile.size() <= nFile)
            {
                vinfoBlockFile.resize(nFile + 1);
            }
        }
        pos.nFile = nFile;
        pos.nPos = vinfoBlockFile[nFile].nSize;
    }

    if ((int)nFile != nLastBlockFile)
    {
        if (!fKnown)
        {
            LogPrintf("Leaving block file %i: %s\n", nLastBlockFile, vinfoBlockFile[nLastBlockFile].ToString());
        }
        FlushBlockFile(!fKnown);
        nLastBlockFile = nFile;
    }

    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    if (fKnown)
        vinfoBlockFile[nFile].nSize = std::max(pos.nPos + nAddSize, vinfoBlockFile[nFile].nSize);
    else
        vinfoBlockFile[nFile].nSize += nAddSize;

    if (!fKnown)
    {
        unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        unsigned int nNewChunks = (vinfoBlockFile[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks)
        {
            if (fPruneMode)
                fCheckForPruning = true;
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos))
            {
                FILE *file = OpenBlockFile(pos);
                if (file)
                {
                    LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE,
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

static bool
CheckBlockHeader(const CBlockHeader &block, CValidationState &state, const Consensus::Params &consensusParams,
                 bool fCheckPOW = true)
{
    // Check proof of work matches claimed amount
    if (fCheckPOW && !CheckProofOfWork(block.GetHash(), block.nBits, consensusParams))
        return state.DoS(50, false, REJECT_INVALID, "high-hash", false, "proof of work failed");

    return true;
}

bool CheckBlock(const CBlock &block, CValidationState &state, const Consensus::Params &consensusParams, bool fCheckPOW,
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
    //    GET_VERIFY_INTERFACE(ifVerifyObj);
    for (const auto &tx : block.vtx)
        if (!(*tx).CheckTransaction(state, false))
            return state.Invalid(false, state.GetRejectCode(), state.GetRejectReason(),
                                 strprintf("Transaction check failed (tx hash %s) %s", tx->GetHash().ToString(),
                                           state.GetDebugMessage()));

    unsigned int nSigOps = 0;
    for (const auto &tx : block.vtx)
    {
        nSigOps += (*tx).GetLegacySigOpCount();
    }
    if (nSigOps * WITNESS_SCALE_FACTOR > MAX_BLOCK_SIGOPS_COST)
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-sigops", false, "out-of-bounds SigOpCount");

    if (fCheckPOW && fCheckMerkleRoot)
        block.fChecked = true;

    return true;
}

bool IsWitnessEnabled(const CBlockIndex *pindexPrev, const Consensus::Params &params)
{
    LOCK(cs_main);
    return (VersionBitsState(pindexPrev, params, Consensus::DEPLOYMENT_SEGWIT, versionbitscache) == THRESHOLD_ACTIVE);
}


// Compute at which vout of the block's coinbase transaction the witness
// commitment occurs, or -1 if not found.
static int GetWitnessCommitmentIndex(const CBlock &block)
{
    int commitpos = -1;
    if (!block.vtx.empty())
    {
        for (size_t o = 0; o < block.vtx[0]->vout.size(); o++)
        {
            if (block.vtx[0]->vout[o].scriptPubKey.size() >= 38 && block.vtx[0]->vout[o].scriptPubKey[0] == OP_RETURN &&
                block.vtx[0]->vout[o].scriptPubKey[1] == 0x24 && block.vtx[0]->vout[o].scriptPubKey[2] == 0xaa &&
                block.vtx[0]->vout[o].scriptPubKey[3] == 0x21 && block.vtx[0]->vout[o].scriptPubKey[4] == 0xa9 &&
                block.vtx[0]->vout[o].scriptPubKey[5] == 0xed)
            {
                commitpos = o;
            }
        }
    }
    return commitpos;
}

void
UpdateUncommittedBlockStructures(CBlock &block, const CBlockIndex *pindexPrev, const Consensus::Params &consensusParams)
{
    int commitpos = GetWitnessCommitmentIndex(block);
    static const std::vector<unsigned char> nonce(32, 0x00);
    if (commitpos != -1 && IsWitnessEnabled(pindexPrev, consensusParams) && !block.vtx[0]->HasWitness())
    {
        CMutableTransaction tx(*block.vtx[0]);
        tx.vin[0].scriptWitness.stack.resize(1);
        tx.vin[0].scriptWitness.stack[0] = nonce;
        block.vtx[0] = MakeTransactionRef(std::move(tx));
    }
}

std::vector<unsigned char>
GenerateCoinbaseCommitment(CBlock &block, const CBlockIndex *pindexPrev, const Consensus::Params &consensusParams)
{
    std::vector<unsigned char> commitment;
    int commitpos = GetWitnessCommitmentIndex(block);
    std::vector<unsigned char> ret(32, 0x00);
    if (consensusParams.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout != 0)
    {
        if (commitpos == -1)
        {
            uint256 witnessroot = BlockWitnessMerkleRoot(block, nullptr);
            CHash256().Write(witnessroot.begin(), 32).Write(ret.data(), 32).Finalize(witnessroot.begin());
            CTxOut out;
            out.nValue = 0;
            out.scriptPubKey.resize(38);
            out.scriptPubKey[0] = OP_RETURN;
            out.scriptPubKey[1] = 0x24;
            out.scriptPubKey[2] = 0xaa;
            out.scriptPubKey[3] = 0x21;
            out.scriptPubKey[4] = 0xa9;
            out.scriptPubKey[5] = 0xed;
            memcpy(&out.scriptPubKey[6], witnessroot.begin(), 32);
            commitment = std::vector<unsigned char>(out.scriptPubKey.begin(), out.scriptPubKey.end());
            CMutableTransaction tx(*block.vtx[0]);
            tx.vout.push_back(out);
            block.vtx[0] = MakeTransactionRef(std::move(tx));
        }
    }
    UpdateUncommittedBlockStructures(block, pindexPrev, consensusParams);
    return commitment;
}

/**
 * BLOCK PRUNING CODE
 */

/* Prune a block file (modify associated database entries)*/
void PruneOneBlockFile(const int fileNumber)
{
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); ++it)
    {
        CBlockIndex *pindex = it->second;
        if (pindex->nFile == fileNumber)
        {
            pindex->nStatus &= ~BLOCK_HAVE_DATA;
            pindex->nStatus &= ~BLOCK_HAVE_UNDO;
            pindex->nFile = 0;
            pindex->nDataPos = 0;
            pindex->nUndoPos = 0;
            setDirtyBlockIndex.insert(pindex);

            // Prune from mapBlocksUnlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // mapBlocksUnlinked or setBlockIndexCandidates.
            std::pair<std::multimap<CBlockIndex *, CBlockIndex *>::iterator, std::multimap<CBlockIndex *, CBlockIndex *>::iterator> range = mapBlocksUnlinked.equal_range(
                    pindex->pprev);
            while (range.first != range.second)
            {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator _it = range.first;
                range.first++;
                if (_it->second == pindex)
                {
                    mapBlocksUnlinked.erase(_it);
                }
            }
        }
    }

    vinfoBlockFile[fileNumber].SetNull();
    setDirtyFileInfo.insert(fileNumber);
}


void UnlinkPrunedFiles(const std::set<int> &setFilesToPrune)
{
    for (std::set<int>::iterator it = setFilesToPrune.begin(); it != setFilesToPrune.end(); ++it)
    {
        CDiskBlockPos pos(*it, 0);
        fs::remove(GetBlockPosFilename(pos, "blk"));
        fs::remove(GetBlockPosFilename(pos, "rev"));
        LogPrintf("Prune: %s deleted blk/rev (%05u)\n", __func__, *it);
    }
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    fs::path path = GetDataDir();
    std::string tmp = path.string();
    uint64_t nFreeBytesAvailable = fs::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}

//static FILE *OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly)
//{
//    if (pos.IsNull())
//        return nullptr;
//    fs::path path = GetBlockPosFilename(pos, prefix);
//    fs::create_directories(path.parent_path());
//    FILE *file = fsbridge::fopen(path, "rb+");
//    if (!file && !fReadOnly)
//        file = fsbridge::fopen(path, "wb+");
//    if (!file)
//    {
//        LogPrintf("Unable to open file %s\n", path.string());
//        return nullptr;
//    }
//    if (pos.nPos)
//    {
//        if (fseek(file, pos.nPos, SEEK_SET))
//        {
//            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
//            fclose(file);
//            return nullptr;
//        }
//    }
//    return file;
//}

//FILE *OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly)
//{
//    return OpenDiskFile(pos, "blk", fReadOnly);
//}

/** Open an undo file (rev?????.dat) */
//static FILE *OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly)
//{
//    return OpenDiskFile(pos, "rev", fReadOnly);
//}

//fs::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix)
//{
//    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
//}

CBlockIndex *InsertBlockIndex(uint256 hash)
{
    if (hash.IsNull())
        return nullptr;

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex *pindexNew = new CBlockIndex();
    if (!pindexNew)
        throw std::runtime_error(std::string(__func__) + ": new CBlockIndex failed");
    mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool LoadGenesisBlock(const CChainParams &chainparams)
{
    LOCK(cs_main);

    // Check whether we're already initialized by checking for genesis in
    // mapBlockIndex. Note that we can't use chainActive here, since it is
    // set based on the coins db, not the block index db, which is the only
    // thing loaded at this point.
    if (mapBlockIndex.count(chainparams.GenesisBlock().GetHash()))
        return true;

    try
    {
        CBlock &block = const_cast<CBlock &>(chainparams.GenesisBlock());
        // Start new block file
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        CValidationState state;
        if (!FindBlockPos(state, blockPos, nBlockSize + 8, 0, block.GetBlockTime()))
            return error("%s: FindBlockPos failed", __func__);
        if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
            return error("%s: writing genesis block to disk failed", __func__);
        CBlockIndex *pindex = AddToBlockIndex(block);
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos, chainparams.GetConsensus()))
            return error("%s: genesis block not accepted", __func__);
    } catch (const std::runtime_error &e)
    {
        return error("%s: failed to write genesis block: %s", __func__, e.what());
    }

    return true;
}

std::string CBlockFileInfo::ToString() const
{
    return strprintf("CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)", nBlocks, nSize, nHeightFirst,
                     nHeightLast, DateTimeStrFormat("%Y-%m-%d", nTimeFirst), DateTimeStrFormat("%Y-%m-%d", nTimeLast));
}

CBlockFileInfo *GetBlockFileInfo(size_t n)
{
    return &vinfoBlockFile.at(n);
}

ThresholdState VersionBitsTipState(const Consensus::Params &params, Consensus::DeploymentPos pos)
{
    LOCK(cs_main);
    return VersionBitsState(chainActive.Tip(), params, pos, versionbitscache);
}

BIP9Stats VersionBitsTipStatistics(const Consensus::Params &params, Consensus::DeploymentPos pos)
{
    LOCK(cs_main);
    return VersionBitsStatistics(chainActive.Tip(), params, pos);
}

int VersionBitsTipStateSinceHeight(const Consensus::Params &params, Consensus::DeploymentPos pos)
{
    LOCK(cs_main);
    return VersionBitsStateSinceHeight(chainActive.Tip(), params, pos, versionbitscache);
}


bool IsSBTCForkEnabled(const Consensus::Params &params, const CBlockIndex *pindex)
{
    return pindex->nHeight >= params.SBTCForkHeight;
}

bool IsSBTCForkEnabled(const Consensus::Params &params, const int height)
{
    return height >= params.SBTCForkHeight;
}

bool IsSBTCForkHeight(const Consensus::Params &params, const int &height)
{
    return params.SBTCForkHeight == height;
}

//! Guess how far we are in the verification process at the given block index
double GuessVerificationProgress(const ChainTxData &data, CBlockIndex *pindex)
{
    if (pindex == nullptr)
        return 0.0;

    int64_t nNow = time(nullptr);

    double fTxTotal;

    if (pindex->nChainTx <= data.nTxCount)
    {
        fTxTotal = data.nTxCount + (nNow - data.nTime) * data.dTxRate;
    } else
    {
        fTxTotal = pindex->nChainTx + (nNow - pindex->GetBlockTime()) * data.dTxRate;
    }

    return pindex->nChainTx / fTxTotal;
}

class CMainCleanup
{
public:
    CMainCleanup()
    {
    }

    ~CMainCleanup()
    {
        // block headers
        BlockMap::iterator it1 = mapBlockIndex.begin();
        for (; it1 != mapBlockIndex.end(); it1++)
            delete (*it1).second;
        mapBlockIndex.clear();
    }
} instance_of_cmaincleanup;
