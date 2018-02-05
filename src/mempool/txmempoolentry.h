//
// Created by root1 on 18-2-5.
//

#ifndef SUPERBITCOIN_TXMEMPOOLITEM_H
#define SUPERBITCOIN_TXMEMPOOLITEM_H

#include "sbtccore/transaction/transaction.h"
/** \class CTxMemPoolEntry
 *
 * CTxMemPoolEntry stores data about the corresponding transaction, as well
 * as data about all in-mempool transactions that depend on the transaction
 * ("descendant" transactions).
 *
 * When a new entry is added to the mempool, we update the descendant state
 * (nCountWithDescendants, nSizeWithDescendants, and nModFeesWithDescendants) for
 * all ancestors of the newly added transaction.
 *
 */
class CBlockIndex;

struct LockPoints
{
    // Will be set to the blockchain height and median time past
    // values that would be necessary to satisfy all relative locktime
    // constraints (BIP68) of this tx given our view of block chain history
    int height;
    int64_t time;
    // As long as the current chain descends from the highest height block
    // containing one of the inputs used in the calculation, then the cached
    // values are still valid even after a reorg.
    CBlockIndex *maxInputBlock;

    LockPoints() : height(0), time(0), maxInputBlock(nullptr)
    {
    }
};

class CTxMemPoolEntry
{
private:
    CTransactionRef tx;
    CAmount nFee;              //!< Cached to avoid expensive parent-transaction lookups
    size_t nTxWeight;          //!< ... and avoid recomputing tx weight (also used for GetTxSize())
    size_t nUsageSize;         //!< ... and total memory usage
    int64_t nTime;             //!< Local time when entering the mempool
    unsigned int entryHeight;  //!< Chain height when entering the mempool
    bool spendsCoinbase;       //!< keep track of transactions that spend a coinbase
    int64_t sigOpCost;         //!< Total sigop cost
    int64_t feeDelta;          //!< Used for determining the priority of the transaction for mining in a block
    LockPoints lockPoints;     //!< Track the height and time at which tx was final

    // Information about descendants of this transaction that are in the
    // mempool; if we remove this transaction we must remove all of these
    // descendants as well.
    uint64_t nCountWithDescendants;  //!< number of descendant transactions
    uint64_t nSizeWithDescendants;   //!< ... and size
    CAmount nModFeesWithDescendants; //!< ... and total fees (all including us)

    // Analogous statistics for ancestor transactions
    uint64_t nCountWithAncestors;
    uint64_t nSizeWithAncestors;
    CAmount nModFeesWithAncestors;
    int64_t nSigOpCostWithAncestors;

public:
    CTxMemPoolEntry(const CTransactionRef &_tx, const CAmount &_nFee,
                    int64_t _nTime, unsigned int _entryHeight,
                    bool spendsCoinbase,
                    int64_t nSigOpsCost, LockPoints lp);

    CTxMemPoolEntry(const CTxMemPoolEntry &other);

    const CTransaction &GetTx() const
    {
        return *this->tx;
    }

    CTransactionRef GetSharedTx() const
    {
        return this->tx;
    }

    const CAmount &GetFee() const
    {
        return nFee;
    }

    size_t GetTxSize() const;

    size_t GetTxWeight() const
    {
        return nTxWeight;
    }

    int64_t GetTime() const
    {
        return nTime;
    }

    unsigned int GetHeight() const
    {
        return entryHeight;
    }

    int64_t GetSigOpCost() const
    {
        return sigOpCost;
    }

    int64_t GetModifiedFee() const
    {
        return nFee + feeDelta;
    }

    size_t DynamicMemoryUsage() const
    {
        return nUsageSize;
    }

    const LockPoints &GetLockPoints() const
    {
        return lockPoints;
    }

    // Adjusts the descendant state.
    void UpdateDescendantState(int64_t modifySize, CAmount modifyFee, int64_t modifyCount);

    // Adjusts the ancestor state
    void UpdateAncestorState(int64_t modifySize, CAmount modifyFee, int64_t modifyCount, int modifySigOps);

    // Updates the fee delta used for mining priority score, and the
    // modified fees with descendants.
    void UpdateFeeDelta(int64_t feeDelta);

    // Update the LockPoints after a reorg
    void UpdateLockPoints(const LockPoints &lp);

    uint64_t GetCountWithDescendants() const
    {
        return nCountWithDescendants;
    }

    uint64_t GetSizeWithDescendants() const
    {
        return nSizeWithDescendants;
    }

    CAmount GetModFeesWithDescendants() const
    {
        return nModFeesWithDescendants;
    }

    bool GetSpendsCoinbase() const
    {
        return spendsCoinbase;
    }

    uint64_t GetCountWithAncestors() const
    {
        return nCountWithAncestors;
    }

    uint64_t GetSizeWithAncestors() const
    {
        return nSizeWithAncestors;
    }

    CAmount GetModFeesWithAncestors() const
    {
        return nModFeesWithAncestors;
    }

    int64_t GetSigOpCostWithAncestors() const
    {
        return nSigOpCostWithAncestors;
    }

    mutable size_t vTxHashesIdx; //!< Index in mempool's vTxHashes
};

#endif //SUPERBITCOIN_TXMEMPOOLITEM_H
