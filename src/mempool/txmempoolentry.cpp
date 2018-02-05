//
// Created by root1 on 18-2-5.
//
#include "txmempoolentry.h"
#include "chaincontrol/validation.h"
#include "sbtccore/core_memusage.h"
#include "sbtccore/transaction/policy.h"
CTxMemPoolEntry::CTxMemPoolEntry(const CTransactionRef &_tx, const CAmount &_nFee,
                                 int64_t _nTime, unsigned int _entryHeight,
                                 bool _spendsCoinbase, int64_t _sigOpsCost, LockPoints lp) :
        tx(_tx), nFee(_nFee), nTime(_nTime), entryHeight(_entryHeight),
        spendsCoinbase(_spendsCoinbase), sigOpCost(_sigOpsCost), lockPoints(lp)
{
    nTxWeight = GetTransactionWeight(*tx);
    nUsageSize = RecursiveDynamicUsage(tx);

    nCountWithDescendants = 1;
    nSizeWithDescendants = GetTxSize();
    nModFeesWithDescendants = nFee;

    feeDelta = 0;

    nCountWithAncestors = 1;
    nSizeWithAncestors = GetTxSize();
    nModFeesWithAncestors = nFee;
    nSigOpCostWithAncestors = sigOpCost;
}

CTxMemPoolEntry::CTxMemPoolEntry(const CTxMemPoolEntry &other)
{
    *this = other;
}

void CTxMemPoolEntry::UpdateFeeDelta(int64_t newFeeDelta)
{
    nModFeesWithDescendants += newFeeDelta - feeDelta;
    nModFeesWithAncestors += newFeeDelta - feeDelta;
    feeDelta = newFeeDelta;
}

void CTxMemPoolEntry::UpdateLockPoints(const LockPoints &lp)
{
    lockPoints = lp;
}

size_t CTxMemPoolEntry::GetTxSize() const
{
    return GetVirtualTransactionSize(nTxWeight, sigOpCost);
}
