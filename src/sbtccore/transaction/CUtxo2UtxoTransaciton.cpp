//
// Created by root1 on 18-2-4.
//

#include <utils/tinyformat.h>
#include <utils/utilmoneystr.h>
#include "transaction.h"
#include "CUtxo2UtxoTransaciton.h"


uint256 CUtxo2UtxoTransaciton::ComputeHash() const
{
    return SerializeHash(*this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_WITNESS);
}

uint256 CUtxo2UtxoTransaciton::GetWitnessHash() const
{
    if (!HasWitness())
    {
        return GetHash();
    }
    return SerializeHash(*this, SER_GETHASH, 0);
}

/* For backward compatibility, the hash is initialized to 0. TODO: remove the need for this default constructor entirely. */
CUtxo2UtxoTransaciton::CUtxo2UtxoTransaciton() : nVersion(CUtxo2UtxoTransaciton::CURRENT_VERSION), vin(), vout(),
                                                 nLockTime(0), hash()
{
}

CUtxo2UtxoTransaciton::CUtxo2UtxoTransaciton(const CMutableTransaction &tx) : nVersion(tx.nVersion), vin(tx.vin),
                                                                              vout(tx.vout),
                                                                              nLockTime(tx.nLockTime),
                                                                              hash(ComputeHash())
{
}

CUtxo2UtxoTransaciton::CUtxo2UtxoTransaciton(CMutableTransaction &&tx) : nVersion(tx.nVersion), vin(std::move(tx.vin)),
                                                                         vout(std::move(tx.vout)),
                                                                         nLockTime(tx.nLockTime),
                                                                         hash(ComputeHash())
{
}

CAmount CUtxo2UtxoTransaciton::GetValueOut() const
{
    CAmount nValueOut = 0;
    for (const auto &tx_out : vout)
    {
        nValueOut += tx_out.nValue;
        if (!MoneyRange(tx_out.nValue) || !MoneyRange(nValueOut))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }
    return nValueOut;
}

unsigned int CUtxo2UtxoTransaciton::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

//std::string CUtxo2UtxoTransaciton::ToString() const
//{
//    return nullptr;
//}

std::string CUtxo2UtxoTransaciton::ToString() const
{
    std::string str;
    str += strprintf("CTransaction(hash=%s, ver=%d, vin.size=%u, vout.size=%u, nLockTime=%u)\n",
                     GetHash().ToString().substr(0, 10),
                     nVersion,
                     vin.size(),
                     vout.size(),
                     nLockTime);
    for (const auto &tx_in : vin)
        str += "    " + tx_in.ToString() + "\n";
    for (const auto &tx_in : vin)
        str += "    " + tx_in.scriptWitness.ToString() + "\n";
    for (const auto &tx_out : vout)
        str += "    " + tx_out.ToString() + "\n";
    return str;
}

bool CUtxo2UtxoTransaciton::Excute(CHECK_TYPE type) const
{
    return false;
}

bool CUtxo2UtxoTransaciton::IsFinalTx(int nBlockHeight, int64_t nBlockTime)
{
    if (nLockTime == 0)
        return true;
    if ((int64_t)nLockTime < ((int64_t)nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const auto &txin : vin)
    {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

unsigned int CUtxo2UtxoTransaciton::GetLegacySigOpCount()
{
    unsigned int nSigOps = 0;
    for (const auto &txin : vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    for (const auto &txout : vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

bool CUtxo2UtxoTransaciton::CheckTransaction(CValidationState &state, bool fCheckDuplicateInputs)
{
    // Basic checks that don't depend on any context
    if (vin.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");
    if (vout.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");
    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    //todo: next must been open
    //    if (::GetSerializeSize(this, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) *
    //        WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
    //        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    for (const auto &txout : vout)
    {
        if (txout.nValue < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs - note that this check is slow so we skip it in CheckBlock
    if (fCheckDuplicateInputs)
    {
        std::set<COutPoint> vInOutPoints;
        for (const auto &txin : vin)
        {
            if (!vInOutPoints.insert(txin.prevout).second)
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
        }
    }

    if (IsCoinBase())
    {
        if (vin[0].scriptSig.size() < 2 || vin[0].scriptSig.size() > 100)
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
    } else
    {
        for (const auto &txin : vin)
            if (txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");
    }

    return true;
}

bool CUtxo2UtxoTransaciton::CheckTxInputs(CValidationState &state, const CCoinsViewCache &inputs, int nSpendHeight)
{
    // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
    // for an attacker to attempt to split the network.
    if (!inputs.HaveInputs(*this))
        return state.Invalid(false, 0, "", "Inputs unavailable");

    CAmount nValueIn = 0;
    CAmount nFees = 0;
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        const COutPoint &prevout = vin[i].prevout;
        const Coin &coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase())
        {
            if (nSpendHeight - coin.nHeight < COINBASE_MATURITY)
                return state.Invalid(false,
                                     REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                                     strprintf("tried to spend coinbase at depth %d", nSpendHeight - coin.nHeight));
        }

        // Check for negative or overflow input values
        nValueIn += coin.out.nValue;
        if (!MoneyRange(coin.out.nValue) || !MoneyRange(nValueIn))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");

    }

    if (nValueIn < GetValueOut())
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
                         strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn),
                                   FormatMoney(GetValueOut())));

    // Tally transaction fees
    CAmount nTxFee = nValueIn - GetValueOut();
    if (nTxFee < 0)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-negative");
    nFees += nTxFee;
    if (!MoneyRange(nFees))
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-outofrange");
    return true;
}
