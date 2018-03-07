// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Super Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txmempool.h"

#include "sbtccore/clientversion.h"
#include "config/consensus.h"
#include "config/argmanager.h"
#include "chaincontrol/validation.h"
#include "block/validation.h"
#include "sbtccore/transaction/policy.h"
#include "wallet/fees.h"
#include "reverse_iterator.h"
#include "sbtccore/streams.h"
#include "timedata.h"
#include "utils/util.h"
#include "utils/utilmoneystr.h"
#include "utils/utiltime.h"
#include "net_processing.h"
#include "wallet/rbf.h"
#include "framework/base.hpp"
#include "interface/inetcomponent.h"
#include "interface/ichaincomponent.h"
#include "utils/net/netmessagehelper.h"
#include "orphantx.h"
#include "chaincontrol/utils.h"

using namespace appbase;

//CTxMemPool::CTxMemPool()
//{
//
//}

CTxMemPool::~CTxMemPool()
{
}

bool CTxMemPool::ComponentInitialize()
{
    std::cout << "initialize CTxMemPool component\n";
    LoadMempool();
    return true;
}

bool CTxMemPool::ComponentStartup()
{
    std::cout << "starting CTxMemPool component \n";
    return true;
}

bool CTxMemPool::ComponentShutdown()
{
    std::cout << "shutdown CTxMemPool component \n";
    return true;
}

bool CTxMemPool::AcceptToMemoryPool(CValidationState &state, const CTransactionRef &tx, bool fLimitFree,
                                    bool *pfMissingInputs, bool fOverrideMempoolLimit, const CAmount nAbsurdFee)
{
    std::list<CTransactionRef> TxnReplaced;
    const CChainParams &chainparams = app().GetChainParams();
    return AcceptToMemoryPoolWithTime(chainparams, state, tx, fLimitFree, pfMissingInputs, GetTime(),
                                      &TxnReplaced, fOverrideMempoolLimit, nAbsurdFee);
}

/** (try to) add transaction to memory pool with a specified acceptance time **/
bool CTxMemPool::AcceptToMemoryPoolWithTime(const CChainParams &chainparams, CValidationState &state,
                                            const CTransactionRef &tx, bool fLimitFree,
                                            bool *pfMissingInputs, int64_t nAcceptTime,
                                            std::list<CTransactionRef> *plTxnReplaced,
                                            bool fOverrideMempoolLimit, const CAmount nAbsurdFee)
{
    std::vector<COutPoint> coins_to_uncache;
    bool res = AcceptToMemoryPoolWorker(chainparams, state, tx, fLimitFree, pfMissingInputs, nAcceptTime,
                                        plTxnReplaced, fOverrideMempoolLimit, nAbsurdFee, coins_to_uncache);
    GET_CHAIN_INTERFACE(ifChainObj);
    if (!res)
    {
        for (const COutPoint &hashTx : coins_to_uncache)
            ifChainObj->GetCoinsTip()->Uncache(hashTx);
    }
    // After we've (potentially) uncached entries, ensure our coins cache is still within its size limits
    CValidationState stateDummy;
    ifChainObj->FlushStateToDisk(stateDummy, FLUSH_STATE_PERIODIC, chainparams);
    return res;
}

bool CTxMemPool::AcceptToMemoryPoolWorker(const CChainParams &chainparams, CValidationState &state,
                                          const CTransactionRef &ptx, bool fLimitFree,
                                          bool *pfMissingInputs, int64_t nAcceptTime,
                                          std::list<CTransactionRef> *plTxnReplaced,
                                          bool fOverrideMempoolLimit, const CAmount &nAbsurdFee,
                                          std::vector<COutPoint> &coins_to_uncache)
{

    const CTransaction &tx = *ptx;
    const uint256 hash = tx.GetHash();
    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;


    // is it already in the memory pool?
    if (this->exists(hash))
    {
        return state.Invalid(false, REJECT_DUPLICATE, "txn-already-in-mempool");
    }

    if (!tx.PreCheck(ENTER_MEMPOOL, state))
    {
        return false;
    }



    //    // Reject transactions with witness before segregated witness activates (override with -prematurewitness)
    //    const CArgsManager &mArgs = appbase::app_bpo().GetArgsManager();
    //    bool witnessEnabled = IsWitnessEnabled(chainActive.Tip(), chainparams.GetConsensus());
    //    if (!mArgs.GetArg<bool>("-prematurewitness", false) && tx.HasWitness() && !witnessEnabled)
    //    {
    //        return state.DoS(0, false, REJECT_NONSTANDARD, "no-witness-yet", true);
    //    }



    //    // Only accept nLockTime-using transactions that can be mined in the next
    //    // block; we don't want our mempool filled up with transactions that can't
    //    // be mined yet.
    //    if (!CheckFinalTx(tx, STANDARD_LOCKTIME_VERIFY_FLAGS))
    //        return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");



    // Check for conflicts with in-memory transactions
    std::set<uint256> setConflicts;
    {
        LOCK(this->cs); // protect pool.mapNextTx
        for (const CTxIn &txin : tx.vin)
        {
            auto itConflicting = this->mapNextTx.find(txin.prevout);
            if (itConflicting != this->mapNextTx.end())
            {
                const CTransaction *ptxConflicting = itConflicting->second;
                if (!setConflicts.count(ptxConflicting->GetHash()))
                {
                    // Allow opt-out of transaction replacement by setting
                    // nSequence > MAX_BIP125_RBF_SEQUENCE (SEQUENCE_FINAL-2) on all inputs.
                    //
                    // SEQUENCE_FINAL-1 is picked to still allow use of nLockTime by
                    // non-replaceable transactions. All inputs rather than just one
                    // is for the sake of multi-party protocols, where we don't
                    // want a single party to be able to disable replacement.
                    //
                    // The opt-out ignores descendants as anyone relying on
                    // first-seen mempool behavior should be checking all
                    // unconfirmed ancestors anyway; doing otherwise is hopelessly
                    // insecure.
                    bool fReplacementOptOut = true;
                    if (fEnableReplacement)
                    {
                        for (const CTxIn &_txin : ptxConflicting->vin)
                        {
                            if (_txin.nSequence <= MAX_BIP125_RBF_SEQUENCE)
                            {
                                fReplacementOptOut = false;
                                break;
                            }
                        }
                    }
                    if (fReplacementOptOut)
                    {
                        return state.Invalid(false, REJECT_DUPLICATE, "txn-mempool-conflict");
                    }

                    setConflicts.insert(ptxConflicting->GetHash());
                }
            }
        }
    }

    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nValueIn = 0;
        LockPoints lp;
        {
            LOCK(this->cs);

            GET_CHAIN_INTERFACE(ifChainObj);
            CCoinsViewCache *pcoinsTip = ifChainObj->GetCoinsTip();

            CCoinsViewMemPool viewMemPool(pcoinsTip, *this);
            view.SetBackend(viewMemPool);

            // do all inputs exist?
            for (const CTxIn txin : tx.vin)
            {
                if (!pcoinsTip->HaveCoinInCache(txin.prevout))
                {
                    coins_to_uncache.push_back(txin.prevout);
                }
                if (!view.HaveCoin(txin.prevout))
                {
                    // Are inputs missing because we already have the tx?
                    for (size_t out = 0; out < tx.vout.size(); out++)
                    {
                        // Optimistically just do efficient check of cache for outputs
                        if (pcoinsTip->HaveCoinInCache(COutPoint(hash, out)))
                        {
                            return state.Invalid(false, REJECT_DUPLICATE, "txn-already-known");
                        }
                    }
                    // Otherwise assume this might be an orphan tx for which we just haven't seen parents yet
                    if (pfMissingInputs)
                    {
                        *pfMissingInputs = true;
                    }
                    return false; // fMissingInputs and !state.IsInvalid() is used to detect this condition, don't set state.Invalid()
                }
            }

            // Bring the best block into scope
            view.GetBestBlock();

            nValueIn = view.GetValueIn(tx);

            // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
            view.SetBackend(dummy);

            // Only accept BIP68 sequence locked transactions that can be mined in the next
            // block; we don't want our mempool filled up with transactions that can't
            // be mined yet.
            // Must keep pool.cs for this unless we change CheckSequenceLocks to take a
            // CoinsViewCache instead of create its own
            if (!CheckSequenceLocks(tx, STANDARD_LOCKTIME_VERIFY_FLAGS, &lp))
                return state.DoS(0, false, REJECT_NONSTANDARD, "non-BIP68-final");
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (!AreInputsStandard(tx, view))
            return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-nonstandard-inputs");

        // Check for non-standard witness in P2WSH
        if (tx.HasWitness() && !IsWitnessStandard(tx, view))
            return state.DoS(0, false, REJECT_NONSTANDARD, "bad-witness-nonstandard", true);

        //        GET_VERIFY_INTERFACE(ifVerifyObj);
        int64_t nSigOpsCost = tx.GetTransactionSigOpCost(view, STANDARD_SCRIPT_VERIFY_FLAGS);

        CAmount nValueOut = tx.GetValueOut();
        CAmount nFees = nValueIn - nValueOut;
        // nModifiedFees includes any fee deltas from PrioritiseTransaction
        CAmount nModifiedFees = nFees;
        this->ApplyDelta(hash, nModifiedFees);

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbase = false;
        for (const CTxIn &txin : tx.vin)
        {
            const Coin &coin = view.AccessCoin(txin.prevout);
            if (coin.IsCoinBase())
            {
                fSpendsCoinbase = true;
                break;
            }
        }

        GET_CHAIN_INTERFACE(ifChainObj);
        CTxMemPoolEntry entry(ptx, nFees, nAcceptTime, ifChainObj->GetActiveChain().Height(),
                              fSpendsCoinbase, nSigOpsCost, lp);
        unsigned int nSize = entry.GetTxSize();

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_STANDARD_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        if (nSigOpsCost > MAX_STANDARD_TX_SIGOPS_COST)
            return state.DoS(0, false, REJECT_NONSTANDARD, "bad-txns-too-many-sigops", false,
                             strprintf("%d", nSigOpsCost));

        const CArgsManager &mArgs = app().GetArgsManager();
        CAmount mempoolRejectFee = this->GetMinFee(
                mArgs.GetArg<uint32_t>("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFee(nSize);
        if (mempoolRejectFee > 0 && nModifiedFees < mempoolRejectFee)
        {
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool min fee not met", false,
                             strprintf("%d < %d", nFees, mempoolRejectFee));
        }

        // No transactions are allowed below minRelayTxFee except from disconnected blocks
        if (fLimitFree && nModifiedFees < ::minRelayTxFee.GetFee(nSize))
        {
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "min relay fee not met");
        }

        if (nAbsurdFee && nFees > nAbsurdFee)
            return state.Invalid(false,
                                 REJECT_HIGHFEE, "absurdly-high-fee",
                                 strprintf("%d > %d", nFees, nAbsurdFee));

        // Calculate in-mempool ancestors, up to a limit.
        CTxMemPool::setEntries setAncestors;
        size_t nLimitAncestors = mArgs.GetArg<uint32_t>("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = mArgs.GetArg<uint32_t>("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT) * 1000;
        size_t nLimitDescendants = mArgs.GetArg<uint32_t>("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize =
                mArgs.GetArg<uint32_t>("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000;
        std::string errString;
        if (!this->CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize,
                                             nLimitDescendants,
                                             nLimitDescendantSize, errString))
        {
            return state.DoS(0, false, REJECT_NONSTANDARD, "too-long-mempool-chain", false, errString);
        }

        // A transaction that spends outputs that would be replaced by it is invalid. Now
        // that we have the set of all ancestors we can detect this
        // pathological case by making sure setConflicts and setAncestors don't
        // intersect.
        for (CTxMemPool::txiter ancestorIt : setAncestors)
        {
            const uint256 &hashAncestor = ancestorIt->GetTx().GetHash();
            if (setConflicts.count(hashAncestor))
            {
                return state.DoS(10, false,
                                 REJECT_INVALID, "bad-txns-spends-conflicting-tx", false,
                                 strprintf("%s spends conflicting transaction %s",
                                           hash.ToString(),
                                           hashAncestor.ToString()));
            }
        }

        // Check if it's economically rational to mine this transaction rather
        // than the ones it replaces.
        CAmount nConflictingFees = 0;
        size_t nConflictingSize = 0;
        uint64_t nConflictingCount = 0;
        CTxMemPool::setEntries allConflicting;

        // If we don't hold the lock allConflicting might be incomplete; the
        // subsequent RemoveStaged() and addUnchecked() calls don't guarantee
        // mempool consistency for us.
        LOCK(this->cs);
        const bool fReplacementTransaction = setConflicts.size();
        if (fReplacementTransaction)
        {
            CFeeRate newFeeRate(nModifiedFees, nSize);
            std::set<uint256> setConflictsParents;
            const int maxDescendantsToVisit = 100;
            CTxMemPool::setEntries setIterConflicting;
            for (const uint256 &hashConflicting : setConflicts)
            {
                CTxMemPool::txiter mi = this->mapTx.find(hashConflicting);
                if (mi == this->mapTx.end())
                    continue;

                // Save these to avoid repeated lookups
                setIterConflicting.insert(mi);

                // Don't allow the replacement to reduce the feerate of the
                // mempool.
                //
                // We usually don't want to accept replacements with lower
                // feerates than what they replaced as that would lower the
                // feerate of the next block. Requiring that the feerate always
                // be increased is also an easy-to-reason about way to prevent
                // DoS attacks via replacements.
                //
                // The mining code doesn't (currently) take children into
                // account (CPFP) so we only consider the feerates of
                // transactions being directly replaced, not their indirect
                // descendants. While that does mean high feerate children are
                // ignored when deciding whether or not to replace, we do
                // require the replacement to pay more overall fees too,
                // mitigating most cases.
                CFeeRate oldFeeRate(mi->GetModifiedFee(), mi->GetTxSize());
                if (newFeeRate <= oldFeeRate)
                {
                    return state.DoS(0, false,
                                     REJECT_INSUFFICIENTFEE, "insufficient fee", false,
                                     strprintf("rejecting replacement %s; new feerate %s <= old feerate %s",
                                               hash.ToString(),
                                               newFeeRate.ToString(),
                                               oldFeeRate.ToString()));
                }

                for (const CTxIn &txin : mi->GetTx().vin)
                {
                    setConflictsParents.insert(txin.prevout.hash);
                }

                nConflictingCount += mi->GetCountWithDescendants();
            }
            // This potentially overestimates the number of actual descendants
            // but we just want to be conservative to avoid doing too much
            // work.
            if (nConflictingCount <= maxDescendantsToVisit)
            {
                // If not too many to replace, then calculate the set of
                // transactions that would have to be evicted
                for (CTxMemPool::txiter it : setIterConflicting)
                {
                    this->CalculateDescendants(it, allConflicting);
                }
                for (CTxMemPool::txiter it : allConflicting)
                {
                    nConflictingFees += it->GetModifiedFee();
                    nConflictingSize += it->GetTxSize();
                }
            } else
            {
                return state.DoS(0, false,
                                 REJECT_NONSTANDARD, "too many potential replacements", false,
                                 strprintf("rejecting replacement %s; too many potential replacements (%d > %d)\n",
                                           hash.ToString(),
                                           nConflictingCount,
                                           maxDescendantsToVisit));
            }

            for (unsigned int j = 0; j < tx.vin.size(); j++)
            {
                // We don't want to accept replacements that require low
                // feerate junk to be mined first. Ideally we'd keep track of
                // the ancestor feerates and make the decision based on that,
                // but for now requiring all new inputs to be confirmed works.
                if (!setConflictsParents.count(tx.vin[j].prevout.hash))
                {
                    // Rather than check the UTXO set - potentially expensive -
                    // it's cheaper to just check if the new input refers to a
                    // tx that's in the mempool.
                    if (this->mapTx.find(tx.vin[j].prevout.hash) != this->mapTx.end())
                        return state.DoS(0, false,
                                         REJECT_NONSTANDARD, "replacement-adds-unconfirmed", false,
                                         strprintf("replacement %s adds unconfirmed input, idx %d",
                                                   hash.ToString(), j));
                }
            }

            // The replacement must pay greater fees than the transactions it
            // replaces - if we did the bandwidth used by those conflicting
            // transactions would not be paid for.
            if (nModifiedFees < nConflictingFees)
            {
                return state.DoS(0, false,
                                 REJECT_INSUFFICIENTFEE, "insufficient fee", false,
                                 strprintf("rejecting replacement %s, less fees than conflicting txs; %s < %s",
                                           hash.ToString(), FormatMoney(nModifiedFees), FormatMoney(nConflictingFees)));
            }

            // Finally in addition to paying more fees than the conflicts the
            // new transaction must pay for its own bandwidth.
            CAmount nDeltaFees = nModifiedFees - nConflictingFees;
            if (nDeltaFees < ::incrementalRelayFee.GetFee(nSize))
            {
                return state.DoS(0, false,
                                 REJECT_INSUFFICIENTFEE, "insufficient fee", false,
                                 strprintf("rejecting replacement %s, not enough additional fees to relay; %s < %s",
                                           hash.ToString(),
                                           FormatMoney(nDeltaFees),
                                           FormatMoney(::incrementalRelayFee.GetFee(nSize))));
            }
        }

        unsigned int scriptVerifyFlags = STANDARD_SCRIPT_VERIFY_FLAGS;
        //        if (!chainparams.RequireStandard())
        //        {
        //            scriptVerifyFlags = mArgs.GetArg<uint32_t>("-promiscuousmempoolflags", scriptVerifyFlags);
        //        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion   attacks.
        PrecomputedTransactionData txdata(tx);
        if (!tx.CheckInputs(state, view, true, scriptVerifyFlags, true, false, txdata))
        {
            // SCRIPT_VERIFY_CLEANSTACK requires SCRIPT_VERIFY_WITNESS, so we
            // need to turn both off, and compare against just turning off CLEANSTACK
            // to see if the failure is specifically due to witness validation.
            CValidationState stateDummy; // Want reported failures to be from first CheckInputs
            if (!tx.HasWitness() && tx.CheckInputs(stateDummy, view, true,
                                                   scriptVerifyFlags &
                                                   ~(SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_CLEANSTACK),
                                                   true, false, txdata) &&
                !tx.CheckInputs(stateDummy, view, true, scriptVerifyFlags & ~SCRIPT_VERIFY_CLEANSTACK, true, false,
                                txdata))
            {
                // Only the witness is missing, so the transaction itself may be fine.
                state.SetCorruptionPossible();
            }
            return false; // state filled in by CheckInputs
        }

        // Check again against the current block tip's script verification
        // flags to cache our script execution flags. This is, of course,
        // useless if the next block has different script flags from the
        // previous one, but because the cache tracks script flags for us it
        // will auto-invalidate and we'll just have a few blocks of extra
        // misses on soft-fork activation.
        //
        // This is also useful in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks (using TestBlockValidity), however allowing such
        // transactions into the mempool can be exploited as a DoS attack.
        //modified by sky unsigned int currentBlockScriptVerifyFlags = GetBlockScriptFlags(chainActive.Tip(), chainparams.GetConsensus());
        unsigned int currentBlockScriptVerifyFlags = 0;
        if (!CheckInputsFromMempoolAndCache(tx, state, view, currentBlockScriptVerifyFlags, true, txdata))
        {
            // If we're using promiscuousmempoolflags, we may hit this normallyd
            // Check if current block has some flags that scriptVerifyFlags
            // does not before printing an ominous warning
            if (!(~scriptVerifyFlags & currentBlockScriptVerifyFlags))
            {
                return error(
                        "%s: BUG! PLEASE REPORT THIS! ConnectInputs failed against latest-block but not STANDARD flags %s, %s",
                        __func__, hash.ToString(), FormatStateMessage(state));
            } else
            {
                if (!tx.CheckInputs(state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS, true, false, txdata))
                {
                    return error(
                            "%s: ConnectInputs failed against MANDATORY but not STANDARD flags due to promiscuous mempool %s, %s",
                            __func__, hash.ToString(), FormatStateMessage(state));
                } else
                {
                    mlog_notice(
                            "Warning: -promiscuousmempool flags set to not include currently enforced soft forks, this may break mining or otherwise cause instability!\n");
                }
            }
        }

        // Remove conflicting transactions from the mempool
        for (const CTxMemPool::txiter it : allConflicting)
        {
            mlog_notice("replacing tx %s with %s for %s BTC additional fees, %d delta bytes",
                        it->GetTx().GetHash().ToString(),
                        hash.ToString(),
                        FormatMoney(nModifiedFees - nConflictingFees),
                        (int)nSize - (int)nConflictingSize);
            if (plTxnReplaced)
                plTxnReplaced->push_back(it->GetSharedTx());
        }
        this->RemoveStaged(allConflicting, false, MemPoolRemovalReason::REPLACED);

        // This transaction should only count for fee estimation if it isn't a
        // BIP 125 replacement transaction (may not be widely supported), the
        // node is not behind, and the transaction is not dependent on any other
        // transactions in the mempool.
        //modified by sky        bool validForFeeEstimation = !fReplacementTransaction && IsCurrentForFeeEstimation() && pool.HasNoInputsOf(tx);
        bool validForFeeEstimation = false;//add by sky
        // Store transaction in memory
        this->addUnchecked(hash, entry, setAncestors, validForFeeEstimation);

        // trim mempool and check if tx was trimmed
        if (!fOverrideMempoolLimit)
        {
            LimitMempoolSize(mArgs.GetArg<uint32_t>("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
                             mArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
            if (!this->exists(hash))
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool full");
        }
    }

    GetMainSignals().TransactionAddedToMempool(ptx);

    return true;
}

/* Make mempool consistent after a reorg, by re-adding or recursively erasing
 * disconnected block transactions from the mempool, and also removing any
 * other transactions from the mempool that are no longer valid given the new
 * tip/height.
 *
 * Note: we assume that disconnectpool only contains transactions that are NOT
 * confirmed in the current chain nor already in the mempool (otherwise,
 * in-mempool descendants of such transactions would be removed).
 *
 * Passing fAddToMempool=false will skip trying to add the transactions back,
 * and instead just erase from the mempool as needed.
 */

void CTxMemPool::UpdateMempoolForReorg(DisconnectedBlockTransactions &disconnectpool, bool fAddToMempool)
{
    AssertLockHeld(cs_main);
    std::vector<uint256> vHashUpdate;
    // disconnectpool's insertion_order index sorts the entries from
    // oldest to newest, but the oldest entry will be the last tx from the
    // latest mined block that was disconnected.
    // Iterate disconnectpool in reverse, so that we add transactions
    // back to the mempool starting with the earliest transaction that had
    // been previously seen in a block.
    auto it = disconnectpool.queuedTx.get<insertion_order>().rbegin();
    while (it != disconnectpool.queuedTx.get<insertion_order>().rend())
    {
        // ignore validation errors in resurrected transactions
        CValidationState stateDummy;
        if (!fAddToMempool || (*it)->IsCoinBase() ||
            !AcceptToMemoryPool(stateDummy, *it, false, nullptr, true))
        {
            // If the transaction doesn't make it in to the mempool, remove any
            // transactions that depend on it (which would now be orphans).
            this->removeRecursive(**it, MemPoolRemovalReason::REORG);
        } else if (this->exists((*it)->GetHash()))
        {
            vHashUpdate.push_back((*it)->GetHash());
        }
        ++it;
    }
    disconnectpool.queuedTx.clear();
    // AcceptToMemoryPool/addUnchecked all assume that new mempool entries have
    // no in-mempool children, which is generally not true when adding
    // previously-confirmed transactions back to the mempool.
    // UpdateTransactionsFromBlock finds descendants of any transactions in
    // the disconnectpool that were added back and cleans up the mempool state.
    this->UpdateTransactionsFromBlock(vHashUpdate);

    GET_CHAIN_INTERFACE(ifChainObj);
    CCoinsViewCache *pcoinsTip = ifChainObj->GetCoinsTip();
    // We also need to remove any now-immature transactions
    this->removeForReorg(pcoinsTip, ifChainObj->GetActiveChain().Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
    // Re-limit mempool size, in case we added any transactions
    LimitMempoolSize(gArgs.GetArg<uint32_t>("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000,
                     gArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
}


void CTxMemPool::LimitMempoolSize(size_t limit, unsigned long age)
{
    int expired = this->Expire(GetTime() - age);
    if (expired != 0)
    {
        mlog_notice("Expired %i transactions from the memory pool\n", expired);
    }

    GET_CHAIN_INTERFACE(ifChainObj);
    CCoinsViewCache *pcoinsTip = ifChainObj->GetCoinsTip();

    std::vector<COutPoint> vNoSpendsRemaining;
    this->TrimToSize(limit, &vNoSpendsRemaining);
    for (const COutPoint &removed : vNoSpendsRemaining)
        pcoinsTip->Uncache(removed);
}

bool CTxMemPool::CheckSequenceLocks(const CTransaction &tx, int flags, LockPoints *lp, bool useExistingLockPoints)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(mempool.cs);

    GET_CHAIN_INTERFACE(ifChainObj);
    CBlockIndex *tip = ifChainObj->GetActiveChain().Tip();
    CBlockIndex index;
    index.pprev = tip;
    // CheckSequenceLocks() uses chainActive.Height()+1 to evaluate
    // height based locks because when SequenceLocks() is called within
    // ConnectBlock(), the height of the block *being*
    // evaluated is what is used.
    // Thus if we want to know if a transaction can be part of the
    // *next* block, we need to use one more than chainActive.Height()
    index.nHeight = tip->nHeight + 1;

    std::pair<int, int64_t> lockPair;
    if (useExistingLockPoints)
    {
        assert(lp);
        lockPair.first = lp->height;
        lockPair.second = lp->time;
    } else
    {
        // pcoinsTip contains the UTXO set for chainActive.Tip()
        CCoinsViewCache *pcoinsTip = ifChainObj->GetCoinsTip();
        CCoinsViewMemPool viewMemPool(pcoinsTip, mempool);
        std::vector<int> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++)
        {
            const CTxIn &txin = tx.vin[txinIndex];
            Coin coin;
            if (!viewMemPool.GetCoin(txin.prevout, coin))
            {
                return error("%s: Missing input", __func__);
            }
            if (coin.nHeight == MEMPOOL_HEIGHT)
            {
                // Assume all mempool transaction confirm in the next block
                prevheights[txinIndex] = tip->nHeight + 1;
            } else
            {
                prevheights[txinIndex] = coin.nHeight;
            }
        }
        //        GET_VERIFY_INTERFACE(ifVerifyObj);
        lockPair = tx.CalculateSequenceLocks(flags, &prevheights, index);
        if (lp)
        {
            lp->height = lockPair.first;
            lp->time = lockPair.second;
            // Also store the hash of the block with the highest height of
            // all the blocks which have sequence locked prevouts.
            // This hash needs to still be on the chain
            // for these LockPoint calculations to be valid
            // Note: It is impossible to correctly calculate a maxInputBlock
            // if any of the sequence locked inputs depend on unconfirmed txs,
            // except in the special case where the relative lock time/height
            // is 0, which is equivalent to no sequence lock. Since we assume
            // input height of tip+1 for mempool txs and test the resulting
            // lockPair from CalculateSequenceLocks against tip+1.  We know
            // EvaluateSequenceLocks will fail if there was a non-zero sequence
            // lock on a mempool input, so we can use the return value of
            // CheckSequenceLocks to indicate the LockPoints validity
            int maxInputHeight = 0;
            for (int height : prevheights)
            {
                // Can ignore mempool inputs since we'll fail if they had non-zero locks
                if (height != tip->nHeight + 1)
                {
                    maxInputHeight = std::max(maxInputHeight, height);
                }
            }
            lp->maxInputBlock = tip->GetAncestor(maxInputHeight);
        }
    }
    return tx.EvaluateSequenceLocks(index, lockPair);
}

bool CTxMemPool::LoadMempool(void)
{
    const CChainParams &chainparams = Params();
    int64_t nExpiryTimeout = gArgs.GetArg<uint32_t>("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60;
    FILE *filestr = fsbridge::fopen(GetDataDir() / "mempool.dat", "rb");
    CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);
    if (file.IsNull())
    {
        mlog_notice("Failed to open mempool file from disk. Continuing anyway.\n");
        return false;
    }

    int64_t count = 0;
    int64_t skipped = 0;
    int64_t failed = 0;
    int64_t nNow = GetTime();

    try
    {
        uint64_t version;
        file >> version;
        if (version != MEMPOOL_DUMP_VERSION)
        {
            return false;
        }
        uint64_t num;
        file >> num;
        while (num--)
        {
            CTransactionRef tx;
            int64_t nTime;
            int64_t nFeeDelta;
            file >> tx;
            file >> nTime;
            file >> nFeeDelta;

            CAmount amountdelta = nFeeDelta;
            if (amountdelta)
            {
                PrioritiseTransaction(tx->GetHash(), amountdelta);
            }
            CValidationState state;
            if (nTime + nExpiryTimeout > nNow)
            {
                LOCK(cs_main);
                AcceptToMemoryPoolWithTime(chainparams, state, tx, true, nullptr, nTime, nullptr, false, 0);
                if (state.IsValid())
                {
                    ++count;
                } else
                {
                    ++failed;
                }
            } else
            {
                ++skipped;
            }
            if (app().ShutdownRequested())
                return false;
        }
        std::map<uint256, CAmount> mapDeltas;
        file >> mapDeltas;

        for (const auto &i : mapDeltas)
        {
            PrioritiseTransaction(i.first, i.second);
        }
    } catch (const std::exception &e)
    {
        mlog_notice("Failed to deserialize mempool data on disk: %s. Continuing anyway.\n", e.what());
        return false;
    }

    mlog_notice("Imported mempool transactions from disk: %i successes, %i failed, %i expired\n", count, failed,
                skipped);
    return true;
}

void CTxMemPool::DumpMempool(void)
{
    int64_t start = GetTimeMicros();

    std::map<uint256, CAmount> mapDeltas;
    std::vector<TxMempoolInfo> vinfo;

    {
        LOCK(mempool.cs);
        for (const auto &i : mempool.mapDeltas)
        {
            mapDeltas[i.first] = i.second;
        }
        vinfo = mempool.infoAll();
    }

    int64_t mid = GetTimeMicros();

    try
    {
        FILE *filestr = fsbridge::fopen(GetDataDir() / "mempool.dat.new", "wb");
        if (!filestr)
        {
            return;
        }

        CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);

        uint64_t version = MEMPOOL_DUMP_VERSION;
        file << version;

        file << (uint64_t)vinfo.size();
        for (const auto &i : vinfo)
        {
            file << *(i.tx);
            file << (int64_t)i.nTime;
            file << (int64_t)i.nFeeDelta;
            mapDeltas.erase(i.tx->GetHash());
        }

        file << mapDeltas;
        FileCommit(file.Get());
        file.fclose();
        RenameOver(GetDataDir() / "mempool.dat.new", GetDataDir() / "mempool.dat");
        int64_t last = GetTimeMicros();
        mlog_notice("Dumped mempool: %gs to copy, %gs to dump\n", (mid - start) * 0.000001, (last - mid) * 0.000001);
    } catch (const std::exception &e)
    {
        mlog_notice("Failed to dump mempool: %s. Continuing anyway.\n", e.what());
    }
}

// Used to avoid mempool polluting consensus critical paths if CCoinsViewMempool
// were somehow broken and returning the wrong scriptPubKeys
bool
CTxMemPool::CheckInputsFromMempoolAndCache(const CTransaction &tx, CValidationState &state, const CCoinsViewCache &view,
                                           unsigned int flags, bool cacheSigStore, PrecomputedTransactionData &txdata)
{
    AssertLockHeld(cs_main);

    // pool.cs should be locked already, but go ahead and re-take the lock here
    // to enforce that mempool doesn't change between when we check the view
    // and when we actually call through to CheckInputs
    LOCK(this->cs);

    assert(!tx.IsCoinBase());

    GET_CHAIN_INTERFACE(ifChainObj);
    CCoinsViewCache *pcoinsTip = ifChainObj->GetCoinsTip();

    for (const CTxIn &txin : tx.vin)
    {
        const Coin &coin = view.AccessCoin(txin.prevout);

        // At this point we haven't actually checked if the coins are all
        // available (or shouldn't assume we have, since CheckInputs does).
        // So we just return failure if the inputs are not available here,
        // and then only have to check equivalence for available inputs.
        if (coin.IsSpent())
            return false;

        const CTransactionRef &txFrom = this->get(txin.prevout.hash);
        if (txFrom)
        {
            assert(txFrom->GetHash() == txin.prevout.hash);
            assert(txFrom->vout.size() > txin.prevout.n);
            assert(txFrom->vout[txin.prevout.n] == coin.out);
        } else
        {
            const Coin &coinFromDisk = pcoinsTip->AccessCoin(txin.prevout);
            assert(!coinFromDisk.IsSpent());
            assert(coinFromDisk.out == coin.out);
        }
    }

    return tx.CheckInputs(state, view, true, flags, cacheSigStore, true, txdata);
}

namespace
{
    /**
     * Filter for transactions that were recently rejected by
     * AcceptToMemoryPool. These are not rerequested until the chain tip
     * changes, at which point the entire filter is reset. Protected by
     * cs_main.
     *
     * Without this filter we'd be re-requesting txs from each of our peers,
     * increasing bandwidth consumption considerably. For instance, with 100
     * peers, half of which relay a tx we don't accept, that might be a 50x
     * bandwidth increase. A flooding attacker attempting to roll-over the
     * filter using minimum-sized, 60byte, transactions might manage to send
     * 1000/sec if we have fast peers, so we pick 120,000 to give our peers a
     * two minute window to send invs to us.
     *
     * Decreasing the false positive rate is fairly cheap, so we pick one in a
     * million to make it highly unlikely for users to have issues with this
     * filter.
     *
     * Memory used: 1.3 MB
     */
    std::unique_ptr<CRollingBloomFilter> recentRejects;
    uint256 hashRecentRejectsChainTip;

    size_t vExtraTxnForCompactIt = 0;
    std::vector<std::pair<uint256, CTransactionRef>> vExtraTxnForCompact GUARDED_BY(cs_main);


    /** Relay map, protected by cs_main. */
    typedef std::map<uint256, CTransactionRef> MapRelay;

    MapRelay mapRelay;

    /** Expiration-time ordered list of (expire time, relay map entry) pairs, protected by cs_main). */
    std::deque<std::pair<int64_t, MapRelay::iterator>> vRelayExpiration;
}

void AddToCompactExtraTransactions(const CTransactionRef &tx)
{
    size_t max_extra_txn = app().GetArgsManager().GetArg<uint32_t>("-blockreconstructionextratxn",
                                                                   DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN);
    if (max_extra_txn <= 0)
        return;
    if (!vExtraTxnForCompact.size())
        vExtraTxnForCompact.resize(max_extra_txn);
    vExtraTxnForCompact[vExtraTxnForCompactIt] = std::make_pair(tx->GetWitnessHash(), tx);
    vExtraTxnForCompactIt = (vExtraTxnForCompactIt + 1) % max_extra_txn;
}

bool CTxMemPool::DoesTransactionExist(uint256 hash)
{
    return true;
}

bool CTxMemPool::NetRequestTxData(ExNode *xnode, uint256 txHash, bool witness, int64_t timeLastMempoolReq)
{
    assert(xnode != nullptr);

    int nSendFlags = witness ? 0 : SERIALIZE_TRANSACTION_NO_WITNESS;

    auto mi = mapRelay.find(txHash);
    if (mi != mapRelay.end())
    {
        SendNetMessage(xnode->nodeID, NetMsgType::TX, xnode->sendVersion, nSendFlags, *mi->second);
        return true;
    }

    auto txinfo = info(txHash);
    if (txinfo.tx && txinfo.nTime <= timeLastMempoolReq)
    {
        SendNetMessage(xnode->nodeID, NetMsgType::TX, xnode->sendVersion, nSendFlags, *txinfo.tx);
        return true;
    }

    return false;
}

log4cpp::Category &CTxMemPool::mlog = log4cpp::Category::getInstance(EMTOSTR(CID_TX_MEMPOOL));

bool CTxMemPool::NetReceiveTxData(ExNode *xnode, CDataStream &stream, uint256 &txHash)
{
    assert(xnode != nullptr);

    const CArgsManager &appArgs = app().GetArgsManager();

    // Stop processing the transaction early if
    // We are in blocks only mode and peer is either not whitelisted or whitelistrelay is off
    if (!IsFlagsBitOn(xnode->flags, NF_RELAYTX) &&
        (!IsFlagsBitOn(xnode->flags, NF_WHITELIST) ||
         !appArgs.GetArg<bool>("-whitelistrelay", DEFAULT_WHITELISTRELAY)))
    {
        mlog_notice("transaction sent in violation of protocol peer=%d", xnode->nodeID);
        return true;
    }

    GET_NET_INTERFACE(ifNetObj);
    assert(ifNetObj != nullptr);

    CTransactionRef ptx;
    stream >> ptx;
    const CTransaction &tx = *ptx;

    txHash = tx.GetHash();
    CInv inv(MSG_TX, txHash);

    std::deque<COutPoint> vWorkQueue;
    std::vector<uint256> vEraseQueue;


    bool fMissingInputs = false;
    CValidationState state;

    std::list<CTransactionRef> lRemovedTxn;

    if (!DoesTransactionExist(tx.GetHash()) &&
        AcceptToMemoryPool(state, ptx, true, &fMissingInputs, &lRemovedTxn))
    {
        GET_CHAIN_INTERFACE(ifChainObj);
        CCoinsViewCache *pcoinsTip = ifChainObj->GetCoinsTip();
        check(pcoinsTip);
        ifNetObj->BroadcastTransaction(tx.GetHash());

        for (unsigned int i = 0; i < tx.vout.size(); i++)
        {
            vWorkQueue.emplace_back(inv.hash, i);
        }

        SetFlagsBit(xnode->retFlags, NF_NEWTRANSACTION);

        mlog_notice("AcceptToMemoryPool: peer=%d: accepted %s (poolsz %u txn, %u kB)",
                    xnode->nodeID,
                    tx.GetHash().ToString(),
                    size(), DynamicMemoryUsage() / 1000);

        // Recursively process any orphan transactions that depended on this one
        std::set<NodeId> setMisbehaving;
        while (!vWorkQueue.empty())
        {
            ITBYPREV itByPrev;
            int ret = COrphanTx::Instance().FindOrphanTransactionsByPrev(&(vWorkQueue.front()), itByPrev);
            vWorkQueue.pop_front();
            if (ret == 0)
                continue;
            for (auto mi = itByPrev->second.begin();
                 mi != itByPrev->second.end();
                 ++mi)
            {
                const CTransactionRef &porphanTx = (*mi)->second.tx;
                const CTransaction &orphanTx = *porphanTx;
                const uint256 &orphanHash = orphanTx.GetHash();
                NodeId fromPeer = (*mi)->second.fromPeer;
                bool fMissingInputs2 = false;
                // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
                // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
                // anyone relaying LegitTxX banned)
                CValidationState stateDummy;


                if (setMisbehaving.count(fromPeer))
                    continue;
                if (AcceptToMemoryPool(stateDummy, porphanTx, true, &fMissingInputs2, &lRemovedTxn))
                {
                    mlog_notice("accepted orphan tx %s", orphanHash.ToString());

                    ifNetObj->BroadcastTransaction(orphanTx.GetHash());

                    for (unsigned int i = 0; i < orphanTx.vout.size(); i++)
                    {
                        vWorkQueue.emplace_back(orphanHash, i);
                    }

                    vEraseQueue.push_back(orphanHash);
                } else if (!fMissingInputs2)
                {
                    int nDos = 0;
                    if (stateDummy.IsInvalid(nDos) && nDos > 0)
                    {
                        // Punish peer that gave us an invalid orphan tx
                        ifNetObj->MisbehaveNode(fromPeer, nDos);
                        setMisbehaving.insert(fromPeer);
                        mlog_notice("invalid orphan tx %s\n", orphanHash.ToString());
                    }
                    // Has inputs but not accepted to mempool
                    // Probably non-standard or insufficient fee
                    mlog_notice("removed orphan tx %s", orphanHash.ToString());
                    vEraseQueue.push_back(orphanHash);
                    if (!orphanTx.HasWitness() && !stateDummy.CorruptionPossible())
                    {
                        // Do not use rejection cache for witness transactions or
                        // witness-stripped transactions, as they can have been malleated.
                        // See https://github.com/bitcoin/bitcoin/issues/8279 for details.
                        assert(recentRejects);
                        recentRejects->insert(orphanHash);
                    }
                }
                check(pcoinsTip);
            }
        }

        for (uint256 hash : vEraseQueue)
            COrphanTx::Instance().EraseOrphanTx(hash);
    } else if (fMissingInputs)
    {
        bool fRejectedParents = false; // It may be the case that the orphans parents have all been rejected
        for (const CTxIn &txin : tx.vin)
        {
            if (recentRejects->contains(txin.prevout.hash))
            {
                fRejectedParents = true;
                break;
            }
        }

        if (!fRejectedParents)
        {
            uint32_t nFetchFlags = 0;
            if (IsFlagsBitOn(xnode->nLocalServices, NODE_WITNESS) &&
                IsFlagsBitOn(xnode->flags, NF_WITNESS))
                nFetchFlags = MSG_WITNESS_FLAG;

            for (const CTxIn &txin : tx.vin)
            {
                CInv _inv(MSG_TX | nFetchFlags, txin.prevout.hash);
                // pfrom->AddInventoryKnown(_inv);
                if (!DoesTransactionExist(_inv.hash))
                    ifNetObj->AskForTransaction(xnode->nodeID, _inv.hash, nFetchFlags);
            }

            COrphanTx::Instance().AddOrphanTx(ptx, xnode->nodeID);

            // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
            unsigned int nMaxOrphanTx = (unsigned int)std::max((int64_t)0,
                                                               int64_t(appArgs.GetArg<uint32_t>("-maxorphantx",
                                                                                                DEFAULT_MAX_ORPHAN_TRANSACTIONS)));
            unsigned int nEvicted = COrphanTx::Instance().LimitOrphanTxSize(nMaxOrphanTx);
            if (nEvicted > 0)
            {
                mlog_notice("mapOrphan overflow, removed %u tx\n", nEvicted);
            }
        } else
        {
            mlog_notice("not keeping orphan with rejected parents %s\n", tx.GetHash().ToString());
            // We will continue to reject this tx since it has rejected
            // parents so avoid re-requesting it from other peers.
            recentRejects->insert(tx.GetHash());
        }
    } else
    {
        if (!tx.HasWitness() && !state.CorruptionPossible())
        {
            // Do not use rejection cache for witness transactions or
            // witness-stripped transactions, as they can have been malleated.
            // See https://github.com/bitcoin/bitcoin/issues/8279 for details.
            assert(recentRejects);
            recentRejects->insert(tx.GetHash());
            if (RecursiveDynamicUsage(*ptx) < 100000)
            {
                AddToCompactExtraTransactions(ptx);
            }
        } else if (tx.HasWitness() && RecursiveDynamicUsage(*ptx) < 100000)
        {
            AddToCompactExtraTransactions(ptx);
        }

        if (IsFlagsBitOn(xnode->flags, NF_WHITELIST) &&
            appArgs.GetArg<bool>("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY))
        {
            // Always relay transactions received from whitelisted peers, even
            // if they were already in the mempool or rejected from it due
            // to policy, allowing the node to function as a gateway for
            // nodes hidden behind it.
            //
            // Never relay transactions that we would assign a non-zero DoS
            // score for, as we expect peers to do the same with us in that
            // case.
            int nDoS = 0;
            if (!state.IsInvalid(nDoS) || nDoS == 0)
            {
                mlog_notice("Force relaying tx %s from whitelisted peer=%d\n", tx.GetHash().ToString(),
                            xnode->nodeID);

                ifNetObj->BroadcastTransaction(tx.GetHash());
            } else
            {
                mlog_notice("Not relaying invalid transaction %s from whitelisted peer=%d (%s)\n",
                            tx.GetHash().ToString(), xnode->nodeID, FormatStateMessage(state));
            }
        }
    }

    for (const CTransactionRef &removedTx : lRemovedTxn)
        AddToCompactExtraTransactions(removedTx);

    int nDoS = 0;
    if (state.IsInvalid(nDoS))
    {
        mlog_error("%s from peer=%d was not accepted: %s", tx.GetHash().ToString(),
                   xnode->nodeID,
                   FormatStateMessage(state));
        if (state.GetRejectCode() > 0 &&
            state.GetRejectCode() < REJECT_INTERNAL) // Never send AcceptToMemoryPool's internal codes over P2P
        {
            SendNetMessage(xnode->nodeID, NetMsgType::REJECT, xnode->sendVersion, 0,
                           std::string(NetMsgType::TX),
                           (unsigned char)state.GetRejectCode(),
                           state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH),
                           inv.hash);
        }


        if (nDoS > 0)
        {
            xnode->nMisbehavior = nDoS;
        }
    }
    return true;
}

bool CTxMemPool::NetRequestTxInventory(ExNode *xnode, bool sendMempool, int64_t minFeeFilter, CBloomFilter *txFilter,
                                       std::vector<uint256> &toSendTxHashes, std::vector<uint256> &haveSentTxHashes)
{
    assert(xnode != nullptr);

    GET_NET_INTERFACE(ifNetObj);
    assert(ifNetObj != nullptr);

    std::vector<CInv> vInv;
    if (sendMempool)
    {
        auto vtxinfo = infoAll();
        for (const auto &txinfo : vtxinfo)
        {
            const uint256 &hash = txinfo.tx->GetHash();
            CInv inv(MSG_TX, hash);

            auto it = std::find(toSendTxHashes.begin(), toSendTxHashes.end(), hash);
            if (it != toSendTxHashes.end())
                toSendTxHashes.erase(it);

            if (txinfo.feeRate.GetFeePerK() < minFeeFilter)
                continue;

            if (txFilter)
                if (!txFilter->IsRelevantAndUpdate(*txinfo.tx))
                    continue;

            haveSentTxHashes.emplace_back(hash);
            vInv.push_back(inv);
            if (vInv.size() == MAX_INV_SZ)
            {
                SendNetMessage(xnode->nodeID, NetMsgType::INV, xnode->sendVersion, 0, vInv);
                vInv.clear();
            }
        }
    }

    if (!toSendTxHashes.empty())
    {
        int64_t nNow = GetTimeMicros();

        // Expire old relay messages
        while (!vRelayExpiration.empty() && vRelayExpiration.front().first < nNow)
        {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Topologically and fee-rate sort the inventory we send for privacy and priority reasons.
        // A heap is used so that not all items need sorting if only a few are being sent.
        auto compFunc = [this](uint256 a, uint256 b)
        { return CompareDepthAndScore(b, a); };
        std::make_heap(toSendTxHashes.begin(), toSendTxHashes.end(), compFunc);

        // No reason to drain out at many times the network's capacity,
        // especially since we have many peers and some will draw much shorter delays.
        unsigned int nRelayedTransactions = 0;

        while (!toSendTxHashes.empty() && nRelayedTransactions < INVENTORY_BROADCAST_MAX)
        {
            // Fetch the top element from the heap
            std::pop_heap(toSendTxHashes.begin(), toSendTxHashes.end(), compFunc);

            uint256 hash = toSendTxHashes.back();
            toSendTxHashes.pop_back();

            auto txinfo = info(hash);
            if (!txinfo.tx)
                continue;

            if (txinfo.feeRate.GetFeePerK() < minFeeFilter)
                continue;

            if (txFilter)
                if (!txFilter->IsRelevantAndUpdate(*txinfo.tx))
                    continue;

            haveSentTxHashes.emplace_back(hash);

            auto ret = mapRelay.insert(std::make_pair(hash, std::move(txinfo.tx)));
            if (ret.second)
            {
                vRelayExpiration.push_back(std::make_pair(nNow + 15 * 60 * 1000000, ret.first));
            }

            vInv.push_back(CInv(MSG_TX, hash));
            nRelayedTransactions++;

            if (vInv.size() == MAX_INV_SZ)
            {
                SendNetMessage(xnode->nodeID, NetMsgType::INV, xnode->sendVersion, 0, vInv);
                vInv.clear();
            }
        }
    }

    if (!vInv.empty())
    {
        SendNetMessage(xnode->nodeID, NetMsgType::INV, xnode->sendVersion, 0, vInv);
    }

    return true;
}

// Update the given tx for any in-mempool descendants.
// Assumes that setMemPoolChildren is correct for the given tx and all
// descendants.
void CTxMemPool::UpdateForDescendants(txiter updateIt, cacheMap &cachedDescendants, const std::set<uint256> &setExclude)
{
    setEntries stageEntries, setAllDescendants;
    stageEntries = GetMemPoolChildren(updateIt);

    while (!stageEntries.empty())
    {
        const txiter cit = *stageEntries.begin();
        setAllDescendants.insert(cit);
        stageEntries.erase(cit);
        const setEntries &setChildren = GetMemPoolChildren(cit);
        for (const txiter childEntry : setChildren)
        {
            cacheMap::iterator cacheIt = cachedDescendants.find(childEntry);
            if (cacheIt != cachedDescendants.end())
            {
                // We've already calculated this one, just add the entries for this set
                // but don't traverse again.
                for (const txiter cacheEntry : cacheIt->second)
                {
                    setAllDescendants.insert(cacheEntry);
                }
            } else if (!setAllDescendants.count(childEntry))
            {
                // Schedule for later processing
                stageEntries.insert(childEntry);
            }
        }
    }
    // setAllDescendants now contains all in-mempool descendants of updateIt.
    // Update and add to cached descendant map
    int64_t modifySize = 0;
    CAmount modifyFee = 0;
    int64_t modifyCount = 0;
    for (txiter cit : setAllDescendants)
    {
        if (!setExclude.count(cit->GetTx().GetHash()))
        {
            modifySize += cit->GetTxSize();
            modifyFee += cit->GetModifiedFee();
            modifyCount++;
            cachedDescendants[updateIt].insert(cit);
            // Update ancestor state for each descendant
            mapTx.modify(cit, update_ancestor_state(updateIt->GetTxSize(), updateIt->GetModifiedFee(), 1,
                                                    updateIt->GetSigOpCost()));
        }
    }
    mapTx.modify(updateIt, update_descendant_state(modifySize, modifyFee, modifyCount));
}

// vHashesToUpdate is the set of transaction hashes from a disconnected block
// which has been re-added to the mempool.
// for each entry, look for descendants that are outside vHashesToUpdate, and
// add fee/size information for such descendants to the parent.
// for each such descendant, also update the ancestor state to include the parent.
void CTxMemPool::UpdateTransactionsFromBlock(const std::vector<uint256> &vHashesToUpdate)
{
    LOCK(cs);
    // For each entry in vHashesToUpdate, store the set of in-mempool, but not
    // in-vHashesToUpdate transactions, so that we don't have to recalculate
    // descendants when we come across a previously seen entry.
    cacheMap mapMemPoolDescendantsToUpdate;

    // Use a set for lookups into vHashesToUpdate (these entries are already
    // accounted for in the state of their ancestors)
    std::set<uint256> setAlreadyIncluded(vHashesToUpdate.begin(), vHashesToUpdate.end());

    // Iterate in reverse, so that whenever we are looking at a transaction
    // we are sure that all in-mempool descendants have already been processed.
    // This maximizes the benefit of the descendant cache and guarantees that
    // setMemPoolChildren will be updated, an assumption made in
    // UpdateForDescendants.
    for (const uint256 &hash : reverse_iterate(vHashesToUpdate))
    {
        // we cache the in-mempool children to avoid duplicate updates
        setEntries setChildren;
        // calculate children from mapNextTx
        txiter it = mapTx.find(hash);
        if (it == mapTx.end())
        {
            continue;
        }
        auto iter = mapNextTx.lower_bound(COutPoint(hash, 0));
        // First calculate the children, and update setMemPoolChildren to
        // include them, and update their setMemPoolParents to include this tx.
        for (; iter != mapNextTx.end() && iter->first->hash == hash; ++iter)
        {
            const uint256 &childHash = iter->second->GetHash();
            txiter childIter = mapTx.find(childHash);
            assert(childIter != mapTx.end());
            // We can skip updating entries we've encountered before or that
            // are in the block (which are already accounted for).
            if (setChildren.insert(childIter).second && !setAlreadyIncluded.count(childHash))
            {
                UpdateChild(it, childIter, true);
                UpdateParent(childIter, it, true);
            }
        }
        UpdateForDescendants(it, mapMemPoolDescendantsToUpdate, setAlreadyIncluded);
    }
}

bool CTxMemPool::CalculateMemPoolAncestors(const CTxMemPoolEntry &entry, setEntries &setAncestors,
                                           uint64_t limitAncestorCount, uint64_t limitAncestorSize,
                                           uint64_t limitDescendantCount, uint64_t limitDescendantSize,
                                           std::string &errString, bool fSearchForParents /* = true */) const
{
    LOCK(cs);

    setEntries parentHashes;
    const CTransaction &tx = entry.GetTx();

    if (fSearchForParents)
    {
        // Get parents of this transaction that are in the mempool
        // GetMemPoolParents() is only valid for entries in the mempool, so we
        // iterate mapTx to find parents.
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            txiter piter = mapTx.find(tx.vin[i].prevout.hash);
            if (piter != mapTx.end())
            {
                parentHashes.insert(piter);
                if (parentHashes.size() + 1 > limitAncestorCount)
                {
                    errString = strprintf("too many unconfirmed parents [limit: %u]", limitAncestorCount);
                    return false;
                }
            }
        }
    } else
    {
        // If we're not searching for parents, we require this to be an
        // entry in the mempool already.
        txiter it = mapTx.iterator_to(entry);
        parentHashes = GetMemPoolParents(it);
    }

    size_t totalSizeWithAncestors = entry.GetTxSize();

    while (!parentHashes.empty())
    {
        txiter stageit = *parentHashes.begin();

        setAncestors.insert(stageit);
        parentHashes.erase(stageit);
        totalSizeWithAncestors += stageit->GetTxSize();

        if (stageit->GetSizeWithDescendants() + entry.GetTxSize() > limitDescendantSize)
        {
            errString = strprintf("exceeds descendant size limit for tx %s [limit: %u]",
                                  stageit->GetTx().GetHash().ToString(), limitDescendantSize);
            return false;
        } else if (stageit->GetCountWithDescendants() + 1 > limitDescendantCount)
        {
            errString = strprintf("too many descendants for tx %s [limit: %u]", stageit->GetTx().GetHash().ToString(),
                                  limitDescendantCount);
            return false;
        } else if (totalSizeWithAncestors > limitAncestorSize)
        {
            errString = strprintf("exceeds ancestor size limit [limit: %u]", limitAncestorSize);
            return false;
        }

        const setEntries &setMemPoolParents = GetMemPoolParents(stageit);
        for (const txiter &phash : setMemPoolParents)
        {
            // If this is a new ancestor, add it.
            if (setAncestors.count(phash) == 0)
            {
                parentHashes.insert(phash);
            }
            if (parentHashes.size() + setAncestors.size() + 1 > limitAncestorCount)
            {
                errString = strprintf("too many unconfirmed ancestors [limit: %u]", limitAncestorCount);
                return false;
            }
        }
    }

    return true;
}

void CTxMemPool::UpdateAncestorsOf(bool add, txiter it, setEntries &setAncestors)
{
    setEntries parentIters = GetMemPoolParents(it);
    // add or remove this tx as a child of each parent
    for (txiter piter : parentIters)
    {
        UpdateChild(piter, it, add);
    }
    const int64_t updateCount = (add ? 1 : -1);
    const int64_t updateSize = updateCount * it->GetTxSize();
    const CAmount updateFee = updateCount * it->GetModifiedFee();
    for (txiter ancestorIt : setAncestors)
    {
        mapTx.modify(ancestorIt, update_descendant_state(updateSize, updateFee, updateCount));
    }
}

void CTxMemPool::UpdateEntryForAncestors(txiter it, const setEntries &setAncestors)
{
    int64_t updateCount = setAncestors.size();
    int64_t updateSize = 0;
    CAmount updateFee = 0;
    int64_t updateSigOpsCost = 0;
    for (txiter ancestorIt : setAncestors)
    {
        updateSize += ancestorIt->GetTxSize();
        updateFee += ancestorIt->GetModifiedFee();
        updateSigOpsCost += ancestorIt->GetSigOpCost();
    }
    mapTx.modify(it, update_ancestor_state(updateSize, updateFee, updateCount, updateSigOpsCost));
}

void CTxMemPool::UpdateChildrenForRemoval(txiter it)
{
    const setEntries &setMemPoolChildren = GetMemPoolChildren(it);
    for (txiter updateIt : setMemPoolChildren)
    {
        UpdateParent(updateIt, it, false);
    }
}

void CTxMemPool::UpdateForRemoveFromMempool(const setEntries &entriesToRemove, bool updateDescendants)
{
    // For each entry, walk back all ancestors and decrement size associated with this
    // transaction
    const uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
    if (updateDescendants)
    {
        // updateDescendants should be true whenever we're not recursively
        // removing a tx and all its descendants, eg when a transaction is
        // confirmed in a block.
        // Here we only update statistics and not data in mapLinks (which
        // we need to preserve until we're finished with all operations that
        // need to traverse the mempool).
        for (txiter removeIt : entriesToRemove)
        {
            setEntries setDescendants;
            CalculateDescendants(removeIt, setDescendants);
            setDescendants.erase(removeIt); // don't update state for self
            int64_t modifySize = -((int64_t)removeIt->GetTxSize());
            CAmount modifyFee = -removeIt->GetModifiedFee();
            int modifySigOps = -removeIt->GetSigOpCost();
            for (txiter dit : setDescendants)
            {
                mapTx.modify(dit, update_ancestor_state(modifySize, modifyFee, -1, modifySigOps));
            }
        }
    }
    for (txiter removeIt : entriesToRemove)
    {
        setEntries setAncestors;
        const CTxMemPoolEntry &entry = *removeIt;
        std::string dummy;
        // Since this is a tx that is already in the mempool, we can call CMPA
        // with fSearchForParents = false.  If the mempool is in a consistent
        // state, then using true or false should both be correct, though false
        // should be a bit faster.
        // However, if we happen to be in the middle of processing a reorg, then
        // the mempool can be in an inconsistent state.  In this case, the set
        // of ancestors reachable via mapLinks will be the same as the set of 
        // ancestors whose packages include this transaction, because when we
        // add a new transaction to the mempool in addUnchecked(), we assume it
        // has no children, and in the case of a reorg where that assumption is
        // false, the in-mempool children aren't linked to the in-block tx's
        // until UpdateTransactionsFromBlock() is called.
        // So if we're being called during a reorg, ie before
        // UpdateTransactionsFromBlock() has been called, then mapLinks[] will
        // differ from the set of mempool parents we'd calculate by searching,
        // and it's important that we use the mapLinks[] notion of ancestor
        // transactions as the set of things to update for removal.
        CalculateMemPoolAncestors(entry, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);
        // Note that UpdateAncestorsOf severs the child links that point to
        // removeIt in the entries for the parents of removeIt.
        UpdateAncestorsOf(false, removeIt, setAncestors);
    }
    // After updating all the ancestor sizes, we can now sever the link between each
    // transaction being removed and any mempool children (ie, update setMemPoolParents
    // for each direct child of a transaction being removed).
    for (txiter removeIt : entriesToRemove)
    {
        UpdateChildrenForRemoval(removeIt);
    }
}

void CTxMemPoolEntry::UpdateDescendantState(int64_t modifySize, CAmount modifyFee, int64_t modifyCount)
{
    nSizeWithDescendants += modifySize;
    assert(int64_t(nSizeWithDescendants) > 0);
    nModFeesWithDescendants += modifyFee;
    nCountWithDescendants += modifyCount;
    assert(int64_t(nCountWithDescendants) > 0);
}

void CTxMemPoolEntry::UpdateAncestorState(int64_t modifySize, CAmount modifyFee, int64_t modifyCount, int modifySigOps)
{
    nSizeWithAncestors += modifySize;
    assert(int64_t(nSizeWithAncestors) > 0);
    nModFeesWithAncestors += modifyFee;
    nCountWithAncestors += modifyCount;
    assert(int64_t(nCountWithAncestors) > 0);
    nSigOpCostWithAncestors += modifySigOps;
    assert(int(nSigOpCostWithAncestors) >= 0);
}

CTxMemPool::CTxMemPool(CBlockPolicyEstimator *estimator) :
        nTransactionsUpdated(0), minerPolicyEstimator(estimator)
{
    _clear(); //lock free clear

    // Sanity checks off by default for performance, because otherwise
    // accepting transactions becomes O(N^2) where N is the number
    // of transactions in the pool
    nCheckFrequency = 0;
}

bool CTxMemPool::isSpent(const COutPoint &outpoint)
{
    LOCK(cs);
    return mapNextTx.count(outpoint);
}

unsigned int CTxMemPool::GetTransactionsUpdated() const
{
    LOCK(cs);
    return nTransactionsUpdated;
}

void CTxMemPool::AddTransactionsUpdated(unsigned int n)
{
    LOCK(cs);
    nTransactionsUpdated += n;
}

bool CTxMemPool::addUnchecked(const uint256 &hash, const CTxMemPoolEntry &entry, setEntries &setAncestors,
                              bool validFeeEstimate)
{
    NotifyEntryAdded(entry.GetSharedTx());
    // Add to memory pool without checking anything.
    // Used by AcceptToMemoryPool(), which DOES do
    // all the appropriate checks.
    LOCK(cs);
    indexed_transaction_set::iterator newit = mapTx.insert(entry).first;
    mapLinks.insert(make_pair(newit, TxLinks()));

    // Update transaction for any feeDelta created by PrioritiseTransaction
    // TODO: refactor so that the fee delta is calculated before inserting
    // into mapTx.
    std::map<uint256, CAmount>::const_iterator pos = mapDeltas.find(hash);
    if (pos != mapDeltas.end())
    {
        const CAmount &delta = pos->second;
        if (delta)
        {
            mapTx.modify(newit, update_fee_delta(delta));
        }
    }

    // Update cachedInnerUsage to include contained transaction's usage.
    // (When we update the entry for in-mempool parents, memory usage will be
    // further updated.)
    cachedInnerUsage += entry.DynamicMemoryUsage();

    const CTransaction &tx = newit->GetTx();
    std::set<uint256> setParentTransactions;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        mapNextTx.insert(std::make_pair(&tx.vin[i].prevout, &tx));
        setParentTransactions.insert(tx.vin[i].prevout.hash);
    }
    // Don't bother worrying about child transactions of this one.
    // Normal case of a new transaction arriving is that there can't be any
    // children, because such children would be orphans.
    // An exception to that is if a transaction enters that used to be in a block.
    // In that case, our disconnect block logic will call UpdateTransactionsFromBlock
    // to clean up the mess we're leaving here.

    // Update ancestors with information about this tx
    for (const uint256 &phash : setParentTransactions)
    {
        txiter pit = mapTx.find(phash);
        if (pit != mapTx.end())
        {
            UpdateParent(newit, pit, true);
        }
    }
    UpdateAncestorsOf(true, newit, setAncestors);
    UpdateEntryForAncestors(newit, setAncestors);

    nTransactionsUpdated++;
    totalTxSize += entry.GetTxSize();
    if (minerPolicyEstimator)
    {
        minerPolicyEstimator->processTransaction(entry, validFeeEstimate);
    }

    vTxHashes.emplace_back(tx.GetWitnessHash(), newit);
    newit->vTxHashesIdx = vTxHashes.size() - 1;

    return true;
}

void CTxMemPool::removeUnchecked(txiter it, MemPoolRemovalReason reason)
{
    NotifyEntryRemoved(it->GetSharedTx(), reason);
    const uint256 hash = it->GetTx().GetHash();
    for (const CTxIn &txin : it->GetTx().vin)
        mapNextTx.erase(txin.prevout);

    if (vTxHashes.size() > 1)
    {
        vTxHashes[it->vTxHashesIdx] = std::move(vTxHashes.back());
        vTxHashes[it->vTxHashesIdx].second->vTxHashesIdx = it->vTxHashesIdx;
        vTxHashes.pop_back();
        if (vTxHashes.size() * 2 < vTxHashes.capacity())
            vTxHashes.shrink_to_fit();
    } else
        vTxHashes.clear();

    totalTxSize -= it->GetTxSize();
    cachedInnerUsage -= it->DynamicMemoryUsage();
    cachedInnerUsage -= memusage::DynamicUsage(mapLinks[it].parents) + memusage::DynamicUsage(mapLinks[it].children);
    mapLinks.erase(it);
    mapTx.erase(it);
    nTransactionsUpdated++;
    if (minerPolicyEstimator)
    {
        minerPolicyEstimator->removeTx(hash, false);
    }
}

// Calculates descendants of entry that are not already in setDescendants, and adds to
// setDescendants. Assumes entryit is already a tx in the mempool and setMemPoolChildren
// is correct for tx and all descendants.
// Also assumes that if an entry is in setDescendants already, then all
// in-mempool descendants of it are already in setDescendants as well, so that we
// can save time by not iterating over those entries.
void CTxMemPool::CalculateDescendants(txiter entryit, setEntries &setDescendants)
{
    setEntries stage;
    if (setDescendants.count(entryit) == 0)
    {
        stage.insert(entryit);
    }
    // Traverse down the children of entry, only adding children that are not
    // accounted for in setDescendants already (because those children have either
    // already been walked, or will be walked in this iteration).
    while (!stage.empty())
    {
        txiter it = *stage.begin();
        setDescendants.insert(it);
        stage.erase(it);

        const setEntries &setChildren = GetMemPoolChildren(it);
        for (const txiter &childiter : setChildren)
        {
            if (!setDescendants.count(childiter))
            {
                stage.insert(childiter);
            }
        }
    }
}

void CTxMemPool::removeRecursive(const CTransaction &origTx, MemPoolRemovalReason reason)
{
    // Remove transaction from memory pool
    {
        LOCK(cs);
        setEntries txToRemove;
        txiter origit = mapTx.find(origTx.GetHash());
        if (origit != mapTx.end())
        {
            txToRemove.insert(origit);
        } else
        {
            // When recursively removing but origTx isn't in the mempool
            // be sure to remove any children that are in the pool. This can
            // happen during chain re-orgs if origTx isn't re-accepted into
            // the mempool for any reason.
            for (unsigned int i = 0; i < origTx.vout.size(); i++)
            {
                auto it = mapNextTx.find(COutPoint(origTx.GetHash(), i));
                if (it == mapNextTx.end())
                    continue;
                txiter nextit = mapTx.find(it->second->GetHash());
                assert(nextit != mapTx.end());
                txToRemove.insert(nextit);
            }
        }
        setEntries setAllRemoves;
        for (txiter it : txToRemove)
        {
            CalculateDescendants(it, setAllRemoves);
        }

        RemoveStaged(setAllRemoves, false, reason);
    }
}

void CTxMemPool::removeForReorg(const CCoinsViewCache *pcoins, unsigned int nMemPoolHeight, int flags)
{
    // Remove transactions spending a coinbase which are now immature and no-longer-final transactions
    LOCK(cs);
    setEntries txToRemove;
    for (indexed_transaction_set::const_iterator it = mapTx.begin(); it != mapTx.end(); it++)
    {
        const CTransaction &tx = it->GetTx();
        LockPoints lp = it->GetLockPoints();
        bool validLP = TestLockPointValidity(&lp);
        if (!CheckFinalTx(tx, flags) || !CheckSequenceLocks(tx, flags, &lp, validLP))
        {
            // Note if CheckSequenceLocks fails the LockPoints may still be invalid
            // So it's critical that we remove the tx and not depend on the LockPoints.
            txToRemove.insert(it);
        } else if (it->GetSpendsCoinbase())
        {
            for (const CTxIn &txin : tx.vin)
            {
                indexed_transaction_set::const_iterator it2 = mapTx.find(txin.prevout.hash);
                if (it2 != mapTx.end())
                    continue;
                const Coin &coin = pcoins->AccessCoin(txin.prevout);
                if (nCheckFrequency != 0)
                    assert(!coin.IsSpent());
                if (coin.IsSpent() ||
                    (coin.IsCoinBase() && ((signed long)nMemPoolHeight) - coin.nHeight < COINBASE_MATURITY))
                {
                    txToRemove.insert(it);
                    break;
                }
            }
        }
        if (!validLP)
        {
            mapTx.modify(it, update_lock_points(lp));
        }
    }
    setEntries setAllRemoves;
    for (txiter it : txToRemove)
    {
        CalculateDescendants(it, setAllRemoves);
    }
    RemoveStaged(setAllRemoves, false, MemPoolRemovalReason::REORG);
}

void CTxMemPool::removeConflicts(const CTransaction &tx)
{
    // Remove transactions which depend on inputs of tx, recursively
    LOCK(cs);
    for (const CTxIn &txin : tx.vin)
    {
        auto it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end())
        {
            const CTransaction &txConflict = *it->second;
            if (txConflict != tx)
            {
                ClearPrioritisation(txConflict.GetHash());
                removeRecursive(txConflict, MemPoolRemovalReason::CONFLICT);
            }
        }
    }
}

/**
 * Called when a block is connected. Removes from mempool and updates the miner fee estimator.
 */
void CTxMemPool::removeForBlock(const std::vector<CTransactionRef> &vtx, unsigned int nBlockHeight)
{
    LOCK(cs);
    std::vector<const CTxMemPoolEntry *> entries;
    for (const auto &tx : vtx)
    {
        uint256 hash = tx->GetHash();

        indexed_transaction_set::iterator i = mapTx.find(hash);
        if (i != mapTx.end())
            entries.push_back(&*i);
    }
    // Before the txs in the new block have been removed from the mempool, update policy estimates
    if (minerPolicyEstimator)
    {
        minerPolicyEstimator->processBlock(nBlockHeight, entries);
    }
    for (const auto &tx : vtx)
    {
        txiter it = mapTx.find(tx->GetHash());
        if (it != mapTx.end())
        {
            setEntries stage;
            stage.insert(it);
            RemoveStaged(stage, true, MemPoolRemovalReason::BLOCK);
        }
        removeConflicts(*tx);
        ClearPrioritisation(tx->GetHash());
    }
    lastRollingFeeUpdate = GetTime();
    blockSinceLastRollingFeeBump = true;
}

void CTxMemPool::_clear()
{
    mapLinks.clear();
    mapTx.clear();
    mapNextTx.clear();
    totalTxSize = 0;
    cachedInnerUsage = 0;
    lastRollingFeeUpdate = GetTime();
    blockSinceLastRollingFeeBump = false;
    rollingMinimumFeeRate = 0;
    ++nTransactionsUpdated;
}

void CTxMemPool::clear()
{
    LOCK(cs);
    _clear();
}

void CTxMemPool::check(const CCoinsViewCache *pcoins) const
{
    if (nCheckFrequency == 0)
        return;

    if (GetRand(std::numeric_limits<uint32_t>::max()) >= nCheckFrequency)
        return;

    mlog_notice("Checking mempool with %u transactions and %u inputs\n", (unsigned int)mapTx.size(),
                (unsigned int)mapNextTx.size());

    uint64_t checkTotal = 0;
    uint64_t innerUsage = 0;

    GET_CHAIN_INTERFACE(ifChainObj);
    CCoinsViewCache mempoolDuplicate(const_cast<CCoinsViewCache *>(pcoins));
    const int64_t nSpendHeight = ifChainObj->GetSpendHeight(mempoolDuplicate);

    LOCK(cs);
    std::list<const CTxMemPoolEntry *> waitingOnDependants;
    //    GET_VERIFY_INTERFACE(ifVerifyObj);
    for (indexed_transaction_set::const_iterator it = mapTx.begin(); it != mapTx.end(); it++)
    {
        unsigned int i = 0;
        checkTotal += it->GetTxSize();
        innerUsage += it->DynamicMemoryUsage();
        const CTransaction &tx = it->GetTx();
        txlinksMap::const_iterator linksiter = mapLinks.find(it);
        assert(linksiter != mapLinks.end());
        const TxLinks &links = linksiter->second;
        innerUsage += memusage::DynamicUsage(links.parents) + memusage::DynamicUsage(links.children);
        bool fDependsWait = false;
        setEntries setParentCheck;
        int64_t parentSizes = 0;
        int64_t parentSigOpCost = 0;
        for (const CTxIn &txin : tx.vin)
        {
            // Check that every mempool transaction's inputs refer to available coins, or other mempool tx's.
            indexed_transaction_set::const_iterator it2 = mapTx.find(txin.prevout.hash);
            if (it2 != mapTx.end())
            {
                const CTransaction &tx2 = it2->GetTx();
                assert(tx2.vout.size() > txin.prevout.n && !tx2.vout[txin.prevout.n].IsNull());
                fDependsWait = true;
                if (setParentCheck.insert(it2).second)
                {
                    parentSizes += it2->GetTxSize();
                    parentSigOpCost += it2->GetSigOpCost();
                }
            } else
            {
                assert(pcoins->HaveCoin(txin.prevout));
            }
            // Check whether its inputs are marked in mapNextTx.
            auto it3 = mapNextTx.find(txin.prevout);
            assert(it3 != mapNextTx.end());
            assert(it3->first == &txin.prevout);
            assert(it3->second == &tx);
            i++;
        }
        assert(setParentCheck == GetMemPoolParents(it));
        // Verify ancestor state is correct.
        setEntries setAncestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        CalculateMemPoolAncestors(*it, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy);
        uint64_t nCountCheck = setAncestors.size() + 1;
        uint64_t nSizeCheck = it->GetTxSize();
        CAmount nFeesCheck = it->GetModifiedFee();
        int64_t nSigOpCheck = it->GetSigOpCost();

        for (txiter ancestorIt : setAncestors)
        {
            nSizeCheck += ancestorIt->GetTxSize();
            nFeesCheck += ancestorIt->GetModifiedFee();
            nSigOpCheck += ancestorIt->GetSigOpCost();
        }

        assert(it->GetCountWithAncestors() == nCountCheck);
        assert(it->GetSizeWithAncestors() == nSizeCheck);
        assert(it->GetSigOpCostWithAncestors() == nSigOpCheck);
        assert(it->GetModFeesWithAncestors() == nFeesCheck);

        // Check children against mapNextTx
        CTxMemPool::setEntries setChildrenCheck;
        auto iter = mapNextTx.lower_bound(COutPoint(it->GetTx().GetHash(), 0));
        int64_t childSizes = 0;
        for (; iter != mapNextTx.end() && iter->first->hash == it->GetTx().GetHash(); ++iter)
        {
            txiter childit = mapTx.find(iter->second->GetHash());
            assert(childit != mapTx.end()); // mapNextTx points to in-mempool transactions
            if (setChildrenCheck.insert(childit).second)
            {
                childSizes += childit->GetTxSize();
            }
        }
        assert(setChildrenCheck == GetMemPoolChildren(it));
        // Also check to make sure size is greater than sum with immediate children.
        // just a sanity check, not definitive that this calc is correct...
        assert(it->GetSizeWithDescendants() >= childSizes + it->GetTxSize());

        if (fDependsWait)
            waitingOnDependants.push_back(&(*it));
        else
        {
            CValidationState state;

            bool fCheckResult = tx.IsCoinBase() ||
                                tx.CheckTxInputs(state, mempoolDuplicate, nSpendHeight);
            assert(fCheckResult);
            ifChainObj->UpdateCoins(tx, mempoolDuplicate, 1000000);
        }
    }
    unsigned int stepsSinceLastRemove = 0;
    while (!waitingOnDependants.empty())
    {
        const CTxMemPoolEntry *entry = waitingOnDependants.front();
        waitingOnDependants.pop_front();
        CValidationState state;
        if (!mempoolDuplicate.HaveInputs(entry->GetTx()))
        {
            waitingOnDependants.push_back(entry);
            stepsSinceLastRemove++;
            assert(stepsSinceLastRemove < waitingOnDependants.size());
        } else
        {
            bool fCheckResult =
                    entry->GetTx().IsCoinBase() || entry->GetTx().CheckTxInputs(state, mempoolDuplicate, nSpendHeight);
            assert(fCheckResult);
            ifChainObj->UpdateCoins(entry->GetTx(), mempoolDuplicate, 1000000);
            stepsSinceLastRemove = 0;
        }
    }
    for (auto it = mapNextTx.cbegin(); it != mapNextTx.cend(); it++)
    {
        uint256 hash = it->second->GetHash();
        indexed_transaction_set::const_iterator it2 = mapTx.find(hash);
        const CTransaction &tx = it2->GetTx();
        assert(it2 != mapTx.end());
        assert(&tx == it->second);
    }

    assert(totalTxSize == checkTotal);
    assert(innerUsage == cachedInnerUsage);
}

bool CTxMemPool::CompareDepthAndScore(const uint256 &hasha, const uint256 &hashb)
{
    LOCK(cs);
    indexed_transaction_set::const_iterator i = mapTx.find(hasha);
    if (i == mapTx.end())
        return false;
    indexed_transaction_set::const_iterator j = mapTx.find(hashb);
    if (j == mapTx.end())
        return true;
    uint64_t counta = i->GetCountWithAncestors();
    uint64_t countb = j->GetCountWithAncestors();
    if (counta == countb)
    {
        return CompareTxMemPoolEntryByScore()(*i, *j);
    }
    return counta < countb;
}

namespace
{
    class DepthAndScoreComparator
    {
    public:
        bool operator()(const CTxMemPool::indexed_transaction_set::const_iterator &a,
                        const CTxMemPool::indexed_transaction_set::const_iterator &b)
        {
            uint64_t counta = a->GetCountWithAncestors();
            uint64_t countb = b->GetCountWithAncestors();
            if (counta == countb)
            {
                return CompareTxMemPoolEntryByScore()(*a, *b);
            }
            return counta < countb;
        }
    };
} // namespace

std::vector<CTxMemPool::indexed_transaction_set::const_iterator> CTxMemPool::GetSortedDepthAndScore() const
{
    std::vector<indexed_transaction_set::const_iterator> iters;
    AssertLockHeld(cs);

    iters.reserve(mapTx.size());

    for (indexed_transaction_set::iterator mi = mapTx.begin(); mi != mapTx.end(); ++mi)
    {
        iters.push_back(mi);
    }
    std::sort(iters.begin(), iters.end(), DepthAndScoreComparator());
    return iters;
}

void CTxMemPool::queryHashes(std::vector<uint256> &vtxid)
{
    LOCK(cs);
    auto iters = GetSortedDepthAndScore();

    vtxid.clear();
    vtxid.reserve(mapTx.size());

    for (auto it : iters)
    {
        vtxid.push_back(it->GetTx().GetHash());
    }
}

static TxMempoolInfo GetInfo(CTxMemPool::indexed_transaction_set::const_iterator it)
{
    return TxMempoolInfo{it->GetSharedTx(), it->GetTime(), CFeeRate(it->GetFee(), it->GetTxSize()),
                         it->GetModifiedFee() - it->GetFee()};
}

std::vector<TxMempoolInfo> CTxMemPool::infoAll() const
{
    LOCK(cs);
    auto iters = GetSortedDepthAndScore();

    std::vector<TxMempoolInfo> ret;
    ret.reserve(mapTx.size());
    for (auto it : iters)
    {
        ret.push_back(GetInfo(it));
    }

    return ret;
}

CTransactionRef CTxMemPool::get(const uint256 &hash) const
{
    LOCK(cs);
    indexed_transaction_set::const_iterator i = mapTx.find(hash);
    if (i == mapTx.end())
        return nullptr;
    return i->GetSharedTx();
}

TxMempoolInfo CTxMemPool::info(const uint256 &hash) const
{
    LOCK(cs);
    indexed_transaction_set::const_iterator i = mapTx.find(hash);
    if (i == mapTx.end())
        return TxMempoolInfo();
    return GetInfo(i);
}

void CTxMemPool::PrioritiseTransaction(const uint256 &hash, const CAmount &nFeeDelta)
{
    {
        LOCK(cs);
        CAmount &delta = mapDeltas[hash];
        delta += nFeeDelta;
        txiter it = mapTx.find(hash);
        if (it != mapTx.end())
        {
            mapTx.modify(it, update_fee_delta(delta));
            // Now update all ancestors' modified fees with descendants
            setEntries setAncestors;
            uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
            std::string dummy;
            CalculateMemPoolAncestors(*it, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);
            for (txiter ancestorIt : setAncestors)
            {
                mapTx.modify(ancestorIt, update_descendant_state(0, nFeeDelta, 0));
            }
            // Now update all descendants' modified fees with ancestors
            setEntries setDescendants;
            CalculateDescendants(it, setDescendants);
            setDescendants.erase(it);
            for (txiter descendantIt : setDescendants)
            {
                mapTx.modify(descendantIt, update_ancestor_state(0, nFeeDelta, 0, 0));
            }
            ++nTransactionsUpdated;
        }
    }
    mlog_notice("PrioritiseTransaction: %s feerate += %s\n", hash.ToString(), FormatMoney(nFeeDelta));
}

void CTxMemPool::ApplyDelta(const uint256 hash, CAmount &nFeeDelta) const
{
    LOCK(cs);
    std::map<uint256, CAmount>::const_iterator pos = mapDeltas.find(hash);
    if (pos == mapDeltas.end())
        return;
    const CAmount &delta = pos->second;
    nFeeDelta += delta;
}

void CTxMemPool::ClearPrioritisation(const uint256 hash)
{
    LOCK(cs);
    mapDeltas.erase(hash);
}

bool CTxMemPool::HasNoInputsOf(const CTransaction &tx) const
{
    for (unsigned int i = 0; i < tx.vin.size(); i++)
        if (exists(tx.vin[i].prevout.hash))
            return false;
    return true;
}

size_t CTxMemPool::DynamicMemoryUsage() const
{
    LOCK(cs);
    // Estimate the overhead of mapTx to be 15 pointers + an allocation, as no exact formula for boost::multi_index_contained is implemented.
    return memusage::MallocUsage(sizeof(CTxMemPoolEntry) + 15 * sizeof(void *)) * mapTx.size() +
           memusage::DynamicUsage(mapNextTx) + memusage::DynamicUsage(mapDeltas) + memusage::DynamicUsage(mapLinks) +
           memusage::DynamicUsage(vTxHashes) + cachedInnerUsage;
}

void CTxMemPool::RemoveStaged(setEntries &stage, bool updateDescendants, MemPoolRemovalReason reason)
{
    AssertLockHeld(cs);
    UpdateForRemoveFromMempool(stage, updateDescendants);
    for (const txiter &it : stage)
    {
        removeUnchecked(it, reason);
    }
}

int CTxMemPool::Expire(int64_t time)
{
    LOCK(cs);
    indexed_transaction_set::index<entry_time>::type::iterator it = mapTx.get<entry_time>().begin();
    setEntries toremove;
    while (it != mapTx.get<entry_time>().end() && it->GetTime() < time)
    {
        toremove.insert(mapTx.project<0>(it));
        it++;
    }
    setEntries stage;
    for (txiter removeit : toremove)
    {
        CalculateDescendants(removeit, stage);
    }
    RemoveStaged(stage, false, MemPoolRemovalReason::EXPIRY);
    return stage.size();
}

bool CTxMemPool::addUnchecked(const uint256 &hash, const CTxMemPoolEntry &entry, bool validFeeEstimate)
{
    LOCK(cs);
    setEntries setAncestors;
    uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
    std::string dummy;
    CalculateMemPoolAncestors(entry, setAncestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy);
    return addUnchecked(hash, entry, setAncestors, validFeeEstimate);
}

void CTxMemPool::UpdateChild(txiter entry, txiter child, bool add)
{
    setEntries s;
    if (add && mapLinks[entry].children.insert(child).second)
    {
        cachedInnerUsage += memusage::IncrementalDynamicUsage(s);
    } else if (!add && mapLinks[entry].children.erase(child))
    {
        cachedInnerUsage -= memusage::IncrementalDynamicUsage(s);
    }
}

void CTxMemPool::UpdateParent(txiter entry, txiter parent, bool add)
{
    setEntries s;
    if (add && mapLinks[entry].parents.insert(parent).second)
    {
        cachedInnerUsage += memusage::IncrementalDynamicUsage(s);
    } else if (!add && mapLinks[entry].parents.erase(parent))
    {
        cachedInnerUsage -= memusage::IncrementalDynamicUsage(s);
    }
}

const CTxMemPool::setEntries &CTxMemPool::GetMemPoolParents(txiter entry) const
{
    assert (entry != mapTx.end());
    txlinksMap::const_iterator it = mapLinks.find(entry);
    assert(it != mapLinks.end());
    return it->second.parents;
}

const CTxMemPool::setEntries &CTxMemPool::GetMemPoolChildren(txiter entry) const
{
    assert (entry != mapTx.end());
    txlinksMap::const_iterator it = mapLinks.find(entry);
    assert(it != mapLinks.end());
    return it->second.children;
}

CFeeRate CTxMemPool::GetMinFee(size_t sizelimit) const
{
    LOCK(cs);
    if (!blockSinceLastRollingFeeBump || rollingMinimumFeeRate == 0)
        return CFeeRate(rollingMinimumFeeRate);

    int64_t time = GetTime();
    if (time > lastRollingFeeUpdate + 10)
    {
        double halflife = ROLLING_FEE_HALFLIFE;
        if (DynamicMemoryUsage() < sizelimit / 4)
            halflife /= 4;
        else if (DynamicMemoryUsage() < sizelimit / 2)
            halflife /= 2;

        rollingMinimumFeeRate = rollingMinimumFeeRate / pow(2.0, (time - lastRollingFeeUpdate) / halflife);
        lastRollingFeeUpdate = time;

        if (rollingMinimumFeeRate < (double)incrementalRelayFee.GetFeePerK() / 2)
        {
            rollingMinimumFeeRate = 0;
            return CFeeRate(0);
        }
    }
    return std::max(CFeeRate(rollingMinimumFeeRate), incrementalRelayFee);
}

void CTxMemPool::trackPackageRemoved(const CFeeRate &rate)
{
    AssertLockHeld(cs);
    if (rate.GetFeePerK() > rollingMinimumFeeRate)
    {
        rollingMinimumFeeRate = rate.GetFeePerK();
        blockSinceLastRollingFeeBump = false;
    }
}

void CTxMemPool::TrimToSize(size_t sizelimit, std::vector<COutPoint> *pvNoSpendsRemaining)
{
    LOCK(cs);

    unsigned nTxnRemoved = 0;
    CFeeRate maxFeeRateRemoved(0);
    while (!mapTx.empty() && DynamicMemoryUsage() > sizelimit)
    {
        indexed_transaction_set::index<descendant_score>::type::iterator it = mapTx.get<descendant_score>().begin();

        // We set the new mempool min fee to the feerate of the removed set, plus the
        // "minimum reasonable fee rate" (ie some value under which we consider txn
        // to have 0 fee). This way, we don't allow txn to enter mempool with feerate
        // equal to txn which were removed with no block in between.
        CFeeRate removed(it->GetModFeesWithDescendants(), it->GetSizeWithDescendants());
        removed += incrementalRelayFee;
        trackPackageRemoved(removed);
        maxFeeRateRemoved = std::max(maxFeeRateRemoved, removed);

        setEntries stage;
        CalculateDescendants(mapTx.project<0>(it), stage);
        nTxnRemoved += stage.size();

        std::vector<CTransaction> txn;
        if (pvNoSpendsRemaining)
        {
            txn.reserve(stage.size());
            for (txiter iter : stage)
                txn.push_back(iter->GetTx());
        }
        RemoveStaged(stage, false, MemPoolRemovalReason::SIZELIMIT);
        if (pvNoSpendsRemaining)
        {
            for (const CTransaction &tx : txn)
            {
                for (const CTxIn &txin : tx.vin)
                {
                    if (exists(txin.prevout.hash))
                        continue;
                    pvNoSpendsRemaining->push_back(txin.prevout);
                }
            }
        }
    }

    if (maxFeeRateRemoved > CFeeRate(0))
    {
        mlog_notice("Removed %u txn, rolling minimum fee bumped to %s\n", nTxnRemoved,
                    maxFeeRateRemoved.ToString());
    }
}

bool CTxMemPool::TransactionWithinChainLimit(const uint256 &txid, size_t chainLimit) const
{
    LOCK(cs);
    auto it = mapTx.find(txid);
    return it == mapTx.end() || (it->GetCountWithAncestors() < chainLimit &&
                                 it->GetCountWithDescendants() < chainLimit);
}

log4cpp::Category &CTxMemPool::getLog()
{
    return mlog;
}

CCoinsViewMemPool::CCoinsViewMemPool(CCoinsView *baseIn, const CTxMemPool &mempoolIn) : CCoinsViewBacked(baseIn),
                                                                                        mempool(mempoolIn)
{
}

bool CCoinsViewMemPool::GetCoin(const COutPoint &outpoint, Coin &coin) const
{
    // If an entry in the mempool exists, always return that one, as it's guaranteed to never
    // conflict with the underlying cache, and it cannot have pruned entries (as it contains full)
    // transactions. First checking the underlying cache risks returning a pruned entry instead.
    CTxMemPool *txmempool = (CTxMemPool *)appbase::CApp::Instance().FindComponent<CTxMemPool>();
    CTransactionRef ptx = txmempool->get(outpoint.hash);
    if (ptx)
    {
        if (outpoint.n < ptx->vout.size())
        {
            coin = Coin(ptx->vout[outpoint.n], MEMPOOL_HEIGHT, false);
            return true;
        } else
        {
            return false;
        }
    }
    return base->GetCoin(outpoint, coin);
}

SaltedTxidHasher::SaltedTxidHasher() : k0(GetRand(std::numeric_limits<uint64_t>::max())),
                                       k1(GetRand(std::numeric_limits<uint64_t>::max()))
{
}
