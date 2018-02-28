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

CBlockIndex *pindexBestHeader = nullptr;
CWaitableCriticalSection csBestBlock;
CConditionVariable cvBlockChange;
int nScriptCheckThreads = 0;
std::atomic_bool fImporting(false);
bool fTxIndex = false;
bool fHavePruned = false;
bool fPruneMode = false;
bool fIsBareMultisigStd = DEFAULT_PERMIT_BAREMULTISIG;
bool fRequireStandard = true;
bool fCheckpointsEnabled = DEFAULT_CHECKPOINTS_ENABLED;
uint64_t nPruneTarget = 0;
int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE;
bool fEnableReplacement = DEFAULT_ENABLE_REPLACEMENT;

arith_uint256 nMinimumChainWork;

CFeeRate minRelayTxFee = CFeeRate(DEFAULT_MIN_RELAY_TX_FEE);
CAmount maxTxFee = DEFAULT_TRANSACTION_MAXFEE;

CBlockPolicyEstimator feeEstimator;
CTxMemPool mempool(&feeEstimator);

/** Constant stuff for coinbase transactions we create: */
CScript COINBASE_FLAGS;

const std::string strMessageMagic = "Bitcoin Signed Message:\n";
// Internal stuff

bool CheckFinalTx(const CTransaction &tx, int flags)
{
    AssertLockHeld(cs_main);
    GET_CHAIN_INTERFACE(ifChainObj);
    CChain &chainActive = ifChainObj->GetActiveChain();

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

    GET_CHAIN_INTERFACE(ifChainObj);
    CChain &chainActive = ifChainObj->GetActiveChain();

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

/** Return transaction in txOut, and if it was found inside a block, its hash is placed in hashBlock */
bool GetTransaction(const uint256 &hash, CTransactionRef &txOut, const Consensus::Params &consensusParams,
                    uint256 &hashBlock, bool fAllowSlow)
{
    CBlockIndex *pindexSlow = nullptr;

    LOCK(cs_main);
    GET_CHAIN_INTERFACE(ifChainObj);
    CChain &chainActive = ifChainObj->GetActiveChain();

    CTransactionRef ptx = mempool.get(hash);
    if (ptx)
    {
        txOut = ptx;
        return true;
    }

    if (fTxIndex)
    {
        CDiskTxPos postx;
        if (ifChainObj->GetBlockTreeDB()->ReadTxIndex(hash, postx))
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
        GET_CHAIN_INTERFACE(ifChainObj);
        CCoinsViewCache *pcoinsTip = ifChainObj->GetCoinsTip();
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

bool CScriptCheck::operator()()
{
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    const CScriptWitness *witness = &ptxTo->vin[nIn].scriptWitness;
    return VerifyScript(scriptSig, scriptPubKey, witness, nFlags,
                        CachingTransactionSignatureChecker(ptxTo, nIn, amount, cacheStore, *txdata), &error);
}

static CuckooCache::cache<uint256, SignatureCacheHasher> scriptExecutionCache;
static uint256 scriptExecutionCacheNonce(GetRandHash());


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

// Protected by cs_main
static ThresholdConditionCache warningcache[VERSIONBITS_NUM_BITS];

bool IsWitnessEnabled(const CBlockIndex *pindexPrev, const Consensus::Params &params)
{
    LOCK(cs_main);
    return (VersionBitsState(pindexPrev, params, Consensus::DEPLOYMENT_SEGWIT, versionbitscache) == THRESHOLD_ACTIVE);
}


// Compute at which vout of the block's coinbase transaction the witness
// commitment occurs, or -1 if not found.
int GetWitnessCommitmentIndex(const CBlock &block)
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





