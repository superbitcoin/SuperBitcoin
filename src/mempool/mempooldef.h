//
// Created by root1 on 18-2-6.
//

#ifndef SUPERBITCOIN_MEMPOOLDEF_H
#define SUPERBITCOIN_MEMPOOLDEF_H

#include <memory>
#include <set>
#include <map>
#include <vector>
#include <utility>
#include <string>

#include "wallet/amount.h"
#include "chaincontrol/coins.h"
#include "chaincontrol/validation.h"
#include "indirectmap.h"
#include "wallet/feerate.h"
#include "transaction/transaction.h"
#include "framework/sync.h"
#include "random.h"
#include "sbtccore/transaction/script/interpreter.h"
#include "sbtccore/block/validation.h"
#include "txmempoolentry.h"

#include "boost/multi_index_container.hpp"
#include "boost/multi_index/ordered_index.hpp"
#include "boost/multi_index/hashed_index.hpp"
#include <boost/multi_index/sequenced_index.hpp>

#include <boost/signals2/signal.hpp>
#include "config/chainparams.h"
#include "comparetxmempoolentry.h"

class CBlockIndex;

/** Fake height value used in Coin to signify they are only in the memory pool (since 0.8) */
static const uint32_t MEMPOOL_HEIGHT = 0x7FFFFFFF;

// Helpers for modifying CTxMemPool::mapTx, which is a boost multi_index.
struct update_descendant_state
{
    update_descendant_state(int64_t _modifySize, CAmount _modifyFee, int64_t _modifyCount) :
            modifySize(_modifySize), modifyFee(_modifyFee), modifyCount(_modifyCount)
    {
    }

    void operator()(CTxMemPoolEntry &e)
    {
        e.UpdateDescendantState(modifySize, modifyFee, modifyCount);
    }

private:
    int64_t modifySize;
    CAmount modifyFee;
    int64_t modifyCount;
};

struct update_ancestor_state
{
    update_ancestor_state(int64_t _modifySize, CAmount _modifyFee, int64_t _modifyCount, int64_t _modifySigOpsCost) :
            modifySize(_modifySize), modifyFee(_modifyFee), modifyCount(_modifyCount),
            modifySigOpsCost(_modifySigOpsCost)
    {
    }

    void operator()(CTxMemPoolEntry &e)
    {
        e.UpdateAncestorState(modifySize, modifyFee, modifyCount, modifySigOpsCost);
    }

private:
    int64_t modifySize;
    CAmount modifyFee;
    int64_t modifyCount;
    int64_t modifySigOpsCost;
};

struct update_fee_delta
{
    update_fee_delta(int64_t _feeDelta) : feeDelta(_feeDelta)
    {
    }

    void operator()(CTxMemPoolEntry &e)
    {
        e.UpdateFeeDelta(feeDelta);
    }

private:
    int64_t feeDelta;
};

struct update_lock_points
{
    update_lock_points(const LockPoints &_lp) : lp(_lp)
    {
    }

    void operator()(CTxMemPoolEntry &e)
    {
        e.UpdateLockPoints(lp);
    }

private:
    const LockPoints &lp;
};

// extracts a transaction hash from CTxMempoolEntry or CTransactionRef
struct mempoolentry_txid
{
    typedef uint256 result_type;

    result_type operator()(const CTxMemPoolEntry &entry) const
    {
        return entry.GetTx().GetHash();
    }

    result_type operator()(const CTransactionRef &tx) const
    {
        return tx->GetHash();
    }
};

// Multi_index tag names
struct descendant_score
{
};
struct entry_time
{
};
struct mining_score
{
};
struct ancestor_score
{
};
//sbtc-vm
struct ancestor_score_or_gas_price
{
};
class CBlockPolicyEstimator;

/**
 * Information about a mempool transaction.
 */
struct TxMempoolInfo
{
    /** The transaction itself */
    CTransactionRef tx;

    /** Time the transaction entered the mempool. */
    int64_t nTime;

    /** Feerate of the transaction. */
    CFeeRate feeRate;

    /** The fee delta. */
    int64_t nFeeDelta;
};

/** Reason why a transaction was removed from the mempool,
 * this is passed to the notification signal.
 */
enum class MemPoolRemovalReason
{
    UNKNOWN = 0, //! Manually removed or unknown reason
    EXPIRY,      //! Expired from mempool
    SIZELIMIT,   //! Removed in size limiting
    REORG,       //! Removed for reorganization
    BLOCK,       //! Removed for block
    CONFLICT,    //! Removed for conflict with in-block transaction
    REPLACED     //! Removed for replacement
};

class SaltedTxidHasher
{
private:
    /** Salt */
    const uint64_t k0, k1;

public:
    SaltedTxidHasher();

    size_t operator()(const uint256 &txid) const
    {
        return SipHashUint256(k0, k1, txid);
    }
};

/**
 * DisconnectedBlockTransactions

 * During the reorg, it's desirable to re-add previously confirmed transactions
 * to the mempool, so that anything not re-confirmed in the new chain is
 * available to be mined. However, it's more efficient to wait until the reorg
 * is complete and process all still-unconfirmed transactions at that time,
 * since we expect most confirmed transactions to (typically) still be
 * confirmed in the new chain, and re-accepting to the memory pool is expensive
 * (and therefore better to not do in the middle of reorg-processing).
 * Instead, store the disconnected transactions (in order!) as we go, remove any
 * that are included in blocks in the new chain, and then process the remaining
 * still-unconfirmed transactions at the end.
 */

// multi_index tag names
struct txid_index
{
};
struct insertion_order
{
};

struct DisconnectedBlockTransactions
{
    typedef boost::multi_index_container<
    CTransactionRef,
    boost::multi_index::indexed_by<
            // sorted by txid
            boost::multi_index::hashed_unique<
            boost::multi_index::tag<txid_index>,
    mempoolentry_txid,
    SaltedTxidHasher
    >,
    // sorted by order in the blockchain
    boost::multi_index::sequenced<
    boost::multi_index::tag<insertion_order>
    >
    >
    > indexed_disconnected_transactions;

    // It's almost certainly a logic bug if we don't clear out queuedTx before
    // destruction, as we add to it while disconnecting blocks, and then we
    // need to re-process remaining transactions to ensure mempool consistency.
    // For now, assert() that we've emptied out this object on destruction.
    // This assert() can always be removed if the reorg-processing code were
    // to be refactored such that this assumption is no longer true (for
    // instance if there was some other way we cleaned up the mempool after a
    // reorg, besides draining this object).
    ~DisconnectedBlockTransactions()
    {
        assert(queuedTx.empty());
    }

    indexed_disconnected_transactions queuedTx;
    uint64_t cachedInnerUsage = 0;

    // Estimate the overhead of queuedTx to be 6 pointers + an allocation, as
    // no exact formula for boost::multi_index_contained is implemented.
    size_t DynamicMemoryUsage() const
    {
        return memusage::MallocUsage(sizeof(CTransactionRef) + 6 * sizeof(void *)) * queuedTx.size() + cachedInnerUsage;
    }

    void addTransaction(const CTransactionRef &tx)
    {
        queuedTx.insert(tx);
        cachedInnerUsage += RecursiveDynamicUsage(tx);
    }

    // Remove entries based on txid_index, and update memory usage.
    void removeForBlock(const std::vector<CTransactionRef> &vtx)
    {
        // Short-circuit in the common case of a block being added to the tip
        if (queuedTx.empty())
        {
            return;
        }
        for (auto const &tx : vtx)
        {
            auto it = queuedTx.find(tx->GetHash());
            if (it != queuedTx.end())
            {
                cachedInnerUsage -= RecursiveDynamicUsage(*it);
                queuedTx.erase(it);
            }
        }
    }

    // Remove an entry by insertion_order index, and update memory usage.
    void removeEntry(indexed_disconnected_transactions::index<insertion_order>::type::iterator entry)
    {
        cachedInnerUsage -= RecursiveDynamicUsage(*entry);
        queuedTx.get<insertion_order>().erase(entry);
    }

    void clear()
    {
        cachedInnerUsage = 0;
        queuedTx.clear();
    }
};

#endif //SUPERBITCOIN_MEMPOOLDEF_H
