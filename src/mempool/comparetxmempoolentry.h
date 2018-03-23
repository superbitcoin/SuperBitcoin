//
// Created by root1 on 18-2-6.
//

#ifndef SUPERBITCOIN_COMPARETXMEMPOOLENTRY_H
#define SUPERBITCOIN_COMPARETXMEMPOOLENTRY_H

#include "txmempoolentry.h"
/** \class CompareTxMemPoolEntryByDescendantScore
 *
 *  Sort an entry by max(score/size of entry's tx, score/size with all descendants).
 */
class CompareTxMemPoolEntryByDescendantScore
{
public:
    bool operator()(const CTxMemPoolEntry &a, const CTxMemPoolEntry &b)
    {
        bool fUseADescendants = UseDescendantScore(a);
        bool fUseBDescendants = UseDescendantScore(b);

        double aModFee = fUseADescendants ? a.GetModFeesWithDescendants() : a.GetModifiedFee();
        double aSize = fUseADescendants ? a.GetSizeWithDescendants() : a.GetTxSize();

        double bModFee = fUseBDescendants ? b.GetModFeesWithDescendants() : b.GetModifiedFee();
        double bSize = fUseBDescendants ? b.GetSizeWithDescendants() : b.GetTxSize();

        // Avoid division by rewriting (a/b > c/d) as (a*d > c*b).
        double f1 = aModFee * bSize;
        double f2 = aSize * bModFee;

        if (f1 == f2)
        {
            return a.GetTime() >= b.GetTime();
        }
        return f1 < f2;
    }

    // Calculate which score to use for an entry (avoiding division).
    bool UseDescendantScore(const CTxMemPoolEntry &a)
    {
        double f1 = (double)a.GetModifiedFee() * a.GetSizeWithDescendants();
        double f2 = (double)a.GetModFeesWithDescendants() * a.GetTxSize();
        return f2 > f1;
    }
};

/** \class CompareTxMemPoolEntryByScore
 *
 *  Sort by score of entry ((fee+delta)/size) in descending order
 */
class CompareTxMemPoolEntryByScore
{
public:
    bool operator()(const CTxMemPoolEntry &a, const CTxMemPoolEntry &b)
    {
        double f1 = (double)a.GetModifiedFee() * b.GetTxSize();
        double f2 = (double)b.GetModifiedFee() * a.GetTxSize();
        if (f1 == f2)
        {
            return b.GetTx().GetHash() < a.GetTx().GetHash();
        }
        return f1 > f2;
    }
};

class CompareTxMemPoolEntryByEntryTime
{
public:
    bool operator()(const CTxMemPoolEntry &a, const CTxMemPoolEntry &b)
    {
        return a.GetTime() < b.GetTime();
    }
};

class CompareTxMemPoolEntryByAncestorFee
{
public:
    bool operator()(const CTxMemPoolEntry &a, const CTxMemPoolEntry &b)
    {
        double aFees = a.GetModFeesWithAncestors();
        double aSize = a.GetSizeWithAncestors();

        double bFees = b.GetModFeesWithAncestors();
        double bSize = b.GetSizeWithAncestors();

        // Avoid division by rewriting (a/b > c/d) as (a*d > c*b).
        double f1 = aFees * bSize;
        double f2 = aSize * bFees;

        if (f1 == f2)
        {
            return a.GetTx().GetHash() < b.GetTx().GetHash();
        }

        return f1 > f2;
    }
};
//sbtc-vm
class CompareTxMemPoolEntryByAncestorFeeOrGasPrice
{
public:
    bool operator()(const CTxMemPoolEntry& a, const CTxMemPoolEntry& b) const
    {
        bool fAHasCreateOrCall = a.GetTx().HasCreateOrCall();
        bool fBHasCreateOrCall = b.GetTx().HasCreateOrCall();

        // If either of the two entries that we are comparing has a contract scriptPubKey, the comparison here takes precedence
        if(fAHasCreateOrCall || fBHasCreateOrCall) {
            // Prioritze non-contract txs
            if(fAHasCreateOrCall != fBHasCreateOrCall) {
                return fAHasCreateOrCall ? false : true;
            }

            // Prioritize the contract txs that have the least number of ancestors
            // The reason for this is that otherwise it is possible to send one tx with a
            // high gas limit but a low gas price which has a child with a low gas limit but a high gas price
            // Without this condition that transaction chain would get priority in being included into the block.
            if(a.GetCountWithAncestors() != b.GetCountWithAncestors()) {
                return a.GetCountWithAncestors() < b.GetCountWithAncestors();
            }

            // Otherwise, prioritize the contract tx with the highest (minimum among its outputs) gas price
            // The reason for using the gas price of the output that sets the minimum gas price is that there
            // otherwise it may be possible to game the prioritization by setting a large gas price in one output
            // that does no execution, while the real execution has a very low gas price
            if(a.GetMinGasPrice() != b.GetMinGasPrice()) {
                return a.GetMinGasPrice() > b.GetMinGasPrice();
            }

            // Otherwise, prioritize the tx with the minimum size
            if(a.GetTxSize() != b.GetTxSize()) {
                return a.GetTxSize() < b.GetTxSize();
            }

            // If the txs are identical in their minimum gas prices and tx size
            // order based on the tx hash for consistency.
            return a.GetTx().GetHash() < b.GetTx().GetHash();
        }

        // If neither of the txs we are comparing are contract txs, use the standard comparison based on ancestor fees / ancestor size
        return CompareTxMemPoolEntryByAncestorFee()(a, b);
    }
};
#endif //SUPERBITCOIN_COMPARETXMEMPOOLENTRY_H
