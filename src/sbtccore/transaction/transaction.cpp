// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chaincontrol/coins.h>
#include <utils/utilmoneystr.h>
#include <sbtccore/transaction/script/interpreter.h>
#include <sbtccore/block/validation.h>
#include <utils/random.h>
#include <sbtccore/transaction/script/sigcache.h>
#include <sbtccore/cuckoocache.h>
#include "transaction.h"
#include "script/scriptcheck.h"
#include "hash.h"
#include "tinyformat.h"
#include "utils/utilstrencodings.h"
#include "chain.h"
#include "policy.h"
#include "base/base.hpp"
#include "interface/ichaincomponent.h"
//#include "block.h"

std::string COutPoint::ToString() const
{
    return strprintf("COutPoint(%s, %u)", hash.ToString().substr(0, 10), n);
}

CTxIn::CTxIn(COutPoint prevoutIn, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CTxIn::CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CTxIn::ToString() const
{
    std::string str;
    str += "CTxIn(";
    str += prevout.ToString();
    if (prevout.IsNull())
        str += strprintf(", coinbase %s", HexStr(scriptSig));
    else
        str += strprintf(", scriptSig=%s", HexStr(scriptSig).substr(0, 24));
    if (nSequence != SEQUENCE_FINAL)
        str += strprintf(", nSequence=%u", nSequence);
    str += ")";
    return str;
}

CTxOut::CTxOut(const CAmount &nValueIn, CScript scriptPubKeyIn)
{
    nValue = nValueIn;
    scriptPubKey = scriptPubKeyIn;
}

std::string CTxOut::ToString() const
{
    return strprintf("CTxOut(nValue=%d.%08d, scriptPubKey=%s)", nValue / COIN, nValue % COIN,
                     HexStr(scriptPubKey).substr(0, 30));
}

CMutableTransaction::CMutableTransaction() : nVersion(CTransaction::CURRENT_VERSION), nLockTime(0)
{
}

CMutableTransaction::CMutableTransaction(const CTransaction &tx) : nVersion(tx.nVersion), vin(tx.vin), vout(tx.vout),
                                                                   nLockTime(tx.nLockTime)
{
}

uint256 CMutableTransaction::GetHash() const
{
    return SerializeHash(*this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_WITNESS);
}

uint256 CTransaction::ComputeHash() const
{
    return SerializeHash(*this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_WITNESS);
}

uint256 CTransaction::GetWitnessHash() const
{
    if (!HasWitness())
    {
        return GetHash();
    }
    return SerializeHash(*this, SER_GETHASH, 0);
}

/* For backward compatibility, the hash is initialized to 0. TODO: remove the need for this default constructor entirely. */
CTransaction::CTransaction() : CTransactionBase(Utxo2UtxoTransaction), nVersion(CTransaction::CURRENT_VERSION), vin(),
                               vout(), nLockTime(0), hash()

//        , cPolicy(*this)
{
}

CTransaction::CTransaction(const CMutableTransaction &tx) : CTransactionBase(Utxo2UtxoTransaction),
                                                            nVersion(tx.nVersion), vin(tx.vin), vout(tx.vout),
                                                            nLockTime(tx.nLockTime), hash(ComputeHash())

//        , cPolicy(*this)
{
}

CTransaction::CTransaction(CMutableTransaction &&tx) : CTransactionBase(Utxo2UtxoTransaction), nVersion(tx.nVersion),
                                                       vin(std::move(tx.vin)),
                                                       vout(std::move(tx.vout)), nLockTime(tx.nLockTime),
                                                       hash(ComputeHash())
//        ,                                                       cPolicy(*this)
{
}

CAmount CTransaction::GetValueOut() const
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

unsigned int CTransaction::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

std::string CTransaction::ToString() const
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

const std::vector<CKeyID> CTransaction::sender() const
{
    return std::vector<CKeyID>();
}

std::vector<CKeyID> CTransaction::to() const
{
    return std::vector<CKeyID>();
}

bool CTransaction::PreCheck(CHECK_TYPE type, CValidationState &state) const
{
    extern bool CheckFinalTx(const CTransaction &tx, int flags);

    if (!CheckTransaction(state)) // state filled in by CheckTransaction
        return false;
    // Coinbase is only valid in a block, not as a loose transaction
    if (IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "coinbase");


    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    std::string reason;
    if (Params().RequireStandard() && !IsStandardTx(*this, reason, true))
        return state.DoS(0, false, REJECT_NONSTANDARD, reason);

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(*this, STANDARD_LOCKTIME_VERIFY_FLAGS))
        return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");


    return true;
}

bool CTransaction::EndCheck(CHECK_TYPE type) const
{
    return false;
}

bool CTransaction::Excute(CHECK_TYPE type) const
{
    return false;
}

bool CTransaction::Undo(CHECK_TYPE type) const
{
    return false;
}

bool CTransaction::CheckTransaction(CValidationState &state, bool fCheckDuplicateInputs) const
{
    // Basic checks that don't depend on any context
    if (vin.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");
    if (vout.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");
    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) *
        WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");

    //sbtc-evm
    GET_CHAIN_INTERFACE(ifChainObj);
    bool enablecontract = false;
    CBlockIndex * pBlockIndex = ifChainObj->GetActiveChain().Tip();
    if(pBlockIndex && pBlockIndex->IsSBTCContractEnabled())
    {
        enablecontract = true;
    }
    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    for (const auto &txout : vout)
    {
        if(enablecontract && IsCoinBase2()){
            if(!(vout.at(0).scriptPubKey.HasOpVmHashState())){
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-hashstate");
            }
        }
        if (txout.nValue < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");

        /////////////////////////////////////////////////////////// // sbtc-vm
        if (txout.scriptPubKey.HasOpCall() || txout.scriptPubKey.HasOpCreate())
        {
            if (!enablecontract)
            {
                return state.DoS(100, false, REJECT_INVALID, "not arrive to the contract height,refuse");
            }
            std::vector<std::vector<unsigned char>> vSolutions;
            txnouttype whichType;
            if (!Solver(txout.scriptPubKey, whichType, vSolutions, true))
            {
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-contract-nonstandard");
            }
        }
        ///////////////////////////////////////////////////////////
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
        {
            if(enablecontract && IsCoinBase2()){
                continue;
            }
            if (txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");

            /////////////////////////////////////////////////////////// // sbtc-vm
            if (txin.scriptSig.HasOpSpend())
            {
                if (!enablecontract)
                {
                    return state.DoS(100, false, REJECT_INVALID, "not arrive to the contract height,refuse");
                }
            }
            ///////////////////////////////////////////////////////////
        }
    }

    return true;
}

bool CTransaction::CheckTxInputs(CValidationState &state, const CCoinsViewCache &inputs,
                                 int nSpendHeight) const
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

unsigned int CTransaction::GetLegacySigOpCount() const
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

unsigned int CTransaction::GetP2SHSigOpCount(const CCoinsViewCache &mapInputs) const
{
    if (IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        const Coin &coin = mapInputs.AccessCoin(vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(vin[i].scriptSig);
    }
    return nSigOps;
}

int64_t CTransaction::GetTransactionSigOpCost(const CCoinsViewCache &inputs, int flags) const
{
    int64_t nSigOps = GetLegacySigOpCount() * WITNESS_SCALE_FACTOR;

    if (IsCoinBase())
        return nSigOps;


    if (flags & SCRIPT_VERIFY_P2SH)
    {
        nSigOps += GetP2SHSigOpCount(inputs) * WITNESS_SCALE_FACTOR;
    }

    for (unsigned int i = 0; i < vin.size(); i++)
    {
        const Coin &coin = inputs.AccessCoin(vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        nSigOps += CountWitnessSigOps(vin[i].scriptSig, prevout.scriptPubKey, &vin[i].scriptWitness, flags);
    }
    return nSigOps;
}

bool CTransaction::IsFinalTx(int nBlockHeight, int64_t nBlockTime) const
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

std::pair<int, int64_t>
CTransaction::CalculateSequenceLocks(int flags, std::vector<int> *prevHeights, const CBlockIndex &block) const
{
    assert(prevHeights->size() == vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(nVersion) >= 2
                         && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68)
    {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < vin.size(); txinIndex++)
    {
        const CTxIn &txin = vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG)
        {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG)
        {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight - 1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK)
                    << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else
        {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool CTransaction::EvaluateSequenceLocks(const CBlockIndex &block, std::pair<int, int64_t> lockPair) const
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool CTransaction::CheckInputs(CValidationState &state, const CCoinsViewCache &inputs,
                               bool fScriptChecks, unsigned int flags, bool cacheSigStore, bool cacheFullScriptStore,
                               PrecomputedTransactionData &txdata, std::vector<CScriptCheck> *pvChecks) const
{

    GET_CHAIN_INTERFACE(ifChainObj);


    if (!IsCoinBase())
    {
        //        GET_VERIFY_INTERFACE(ifVerifyObj);
        if (!CheckTxInputs(state, inputs, ifChainObj->GetSpendHeight(inputs)))
            return false;

        if (pvChecks)
            pvChecks->reserve(vin.size());

        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.

        // Skip script verification when connecting blocks under the
        // assumevalid block. Assuming the assumevalid block is valid this
        // is safe because block merkle hashes are still computed and checked,
        // Of course, if an assumed valid block is invalid due to false scriptSigs
        // this optimization would allow an invalid chain to be accepted.
        if (fScriptChecks)
        {
            // First check if script executions have been cached with the same
            // flags. Note that this assumes that the inputs provided are
            // correct (ie that the transaction hash which is in tx's prevouts
            // properly commits to the scriptPubKey in the inputs view of that
            // transaction).
            uint256 hashCacheEntry;
            // We only use the first 19 bytes of nonce to avoid a second SHA
            // round - giving us 19 + 32 + 4 = 55 bytes (+ 8 + 1 = 64)
            static_assert(55 - sizeof(flags) - 32 >= 128 / 8,
                          "Want at least 128 bits of nonce for script execution cache");
            CSHA256().Write(scriptExecutionCacheNonce.begin(), 55 - sizeof(flags) - 32).Write(
                    GetWitnessHash().begin(), 32).Write((unsigned char *)&flags, sizeof(flags)).Finalize(
                    hashCacheEntry.begin());
            AssertLockHeld(cs_main); //TODO: Remove this requirement by making CuckooCache not require external locks
            if (scriptExecutionCache.contains(hashCacheEntry, !cacheFullScriptStore))
            {
                return true;
            }

            for (unsigned int i = 0; i < vin.size(); i++)
            {
                const COutPoint &prevout = vin[i].prevout;
                const Coin &coin = inputs.AccessCoin(prevout);
                assert(!coin.IsSpent());

                // We very carefully only pass in things to CScriptCheck which
                // are clearly committed to by tx' witness hash. This provides
                // a sanity check that our caching is not introducing consensus
                // failures through additional data in, eg, the coins being
                // spent being checked as a part of CScriptCheck.
                const CScript &scriptPubKey = coin.out.scriptPubKey;
                const CAmount amount = coin.out.nValue;

                // Verify signature
                CScriptCheck check(scriptPubKey, amount, *this, i, flags, cacheSigStore, &txdata);
                if (pvChecks)
                {
                    pvChecks->push_back(CScriptCheck());
                    check.swap(pvChecks->back());
                } else if (!check())
                {
                    if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS)
                    {
                        // Check whether the failure was caused by a
                        // non-mandatory script verification check, such as
                        // non-standard DER encodings or non-null dummy
                        // arguments; if so, don't trigger DoS protection to
                        // avoid splitting the network between upgraded and
                        // non-upgraded nodes.
                        CScriptCheck check2(scriptPubKey, amount, *this, i,
                                            flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, cacheSigStore, &txdata);
                        if (check2())
                            return state.Invalid(false, REJECT_NONSTANDARD,
                                                 strprintf("non-mandatory-script-verify-flag (%s)",
                                                           ScriptErrorString(check.GetScriptError())));
                    }
                    // Failures of other flags indicate a transaction that is
                    // invalid in new blocks, e.g. an invalid P2SH. We DoS ban
                    // such nodes as they are not following the protocol. That
                    // said during an upgrade careful thought should be taken
                    // as to the correct behavior - we may want to continue
                    // peering with non-upgraded nodes even after soft-fork
                    // super-majority signaling has occurred.
                    return state.DoS(100, false, REJECT_INVALID, strprintf("mandatory-script-verify-flag-failed (%s)",
                                                                           ScriptErrorString(check.GetScriptError())));
                }
            }

            if (cacheFullScriptStore && !pvChecks)
            {
                // We executed all of the provided scripts, and were told to
                // cache the result. Do so now.
                scriptExecutionCache.insert(hashCacheEntry);
            }
        }
    }

    return true;
}

bool CTransaction::SequenceLocks(int flags, std::vector<int> *prevHeights, const CBlockIndex &block) const
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(flags, prevHeights, block));
}


///////////////////////////////////////////////////////////// //sbtc-vm
bool CTransaction::HasCreateOrCall() const
{

    bool IsEnabled =  [&]()->bool{
        GET_CHAIN_INTERFACE(ifChainObj);
        if(ifChainObj->GetActiveChain().Tip()== nullptr) return false;
        return ifChainObj->GetActiveChain().Tip()->IsSBTCContractEnabled();
    }();

    if (!IsEnabled)
    {
        return false;
    }
    for (const CTxOut &v : vout)
    {
        if (v.scriptPubKey.HasOpCreate() || v.scriptPubKey.HasOpCall())
        {
            return true;
        }
    }
    return false;
}

bool CTransaction::HasOpSpend() const
{
    bool IsEnabled =  [&]()->bool{
        GET_CHAIN_INTERFACE(ifChainObj);
        if(ifChainObj->GetActiveChain().Tip()== nullptr) return false;
        return ifChainObj->GetActiveChain().Tip()->IsSBTCContractEnabled();
    }();

    if (!IsEnabled)
    {
        return false;
    }
    for (const CTxIn &i : vin)
    {
        if (i.scriptSig.HasOpSpend())
        {
            return true;
        }
    }
    return false;
}

bool CTransaction::CheckSenderScript(const CCoinsViewCache &view) const
{

    bool IsEnabled =  [&]()->bool{
        GET_CHAIN_INTERFACE(ifChainObj);
        if(ifChainObj->GetActiveChain().Tip()== nullptr) return false;
        return ifChainObj->GetActiveChain().Tip()->IsSBTCContractEnabled();
    }();

    if (!IsEnabled)
    {
        return false;
    }
    CScript script = view.AccessCoin(vin[0].prevout).out.scriptPubKey;
    if (!script.IsPayToPubkeyHash() && !script.IsPayToPubkey())
    {
        return false;
    }
    return true;
}
/////////////////////////////////////////////////////////////
