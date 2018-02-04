//
// Created by root1 on 18-2-4.
//

#ifndef SUPERBITCOIN_CUTXO2UTXOTRANSACITON_H
#define SUPERBITCOIN_CUTXO2UTXOTRANSACITON_H


#include <chaincontrol/validation.h>
#include "transaction.h"
#include "CTransactionBase.h"
#include <chaincontrol/coins.h>

class CCoinsViewCache;

class CUtxo2UtxoTransaciton : public CTransactionBase
{
public:
    // Default transaction version.
    static const int32_t CURRENT_VERSION = 2;

    // Changing the default transaction version requires a two step process: first
    // adapting relay policy by bumping MAX_STANDARD_VERSION, and then later date
    // bumping the default CURRENT_VERSION at which point both CURRENT_VERSION and
    // MAX_STANDARD_VERSION will be equal.
    static const int32_t MAX_STANDARD_VERSION = 2;

    // The local variables are made const to prevent unintended modification
    // without updating the cached hash value. However, CTransaction is not
    // actually immutable; deserialization and assignment are implemented,
    // and bypass the constness. This is safe, as they update the entire
    // structure, including the hash.
    const int32_t nVersion;
    const std::vector<CTxIn> vin;
    const std::vector<CTxOut> vout;
    const uint32_t nLockTime;


    bool Excute() const override;


    bool IsFinalTx(int nBlockHeight, int64_t nBlockTime);

    unsigned int GetLegacySigOpCount();

    bool CheckTransaction(CValidationState &state, bool fCheckDuplicateInputs);

    /**
      * Check whether all inputs of this transaction are valid (no double spends and amounts)
      * This does not modify the UTXO set. This does not check scripts and sigs.
      * Preconditions: tx.IsCoinBase() is false.
      */
    bool CheckTxInputs(CValidationState &state, const CCoinsViewCache &inputs, int nSpendHeight);

private:
    /** Memory only. */
    const uint256 hash;

    uint256 ComputeHash() const;

public:
    /** Construct a CTransaction that qualifies as IsNull() */
    CUtxo2UtxoTransaciton();

    /** Convert a CMutableTransaction into a CTransaction. */
    CUtxo2UtxoTransaciton(const CMutableTransaction &tx);

    CUtxo2UtxoTransaciton(CMutableTransaction &&tx);

    template<typename Stream>
    inline void Serialize(Stream &s) const
    {
        SerializeTransaction(*this, s);
    }

    /** This deserializing constructor is provided instead of an Unserialize method.
     *  Unserialize is not possible, since it would require overwriting const fields. */
    template<typename Stream>
    CUtxo2UtxoTransaciton(deserialize_type, Stream &s) : CUtxo2UtxoTransaciton(CMutableTransaction(deserialize, s))
    {
    }

    bool IsNull() const
    {
        return vin.empty() && vout.empty();
    }

    const uint256 &GetHash() const
    {
        return hash;
    }

    // Compute a hash that includes both transaction and witness data
    uint256 GetWitnessHash() const;

    // Return sum of txouts.
    CAmount GetValueOut() const;
    // GetValueIn() is a method on CCoinsViewCache, because
    // inputs must be known to compute value in.

    /**
     * Get the total transaction size in bytes, including witness data.
     * "Total Size" defined in BIP141 and BIP144.
     * @return Total transaction size in bytes
     */
    unsigned int GetTotalSize() const;

    bool IsCoinBase() const
    {
        return (vin.size() == 1 && vin[0].prevout.IsNull());
    }

    friend bool operator==(const CUtxo2UtxoTransaciton &a, const CUtxo2UtxoTransaciton &b)
    {
        return a.hash == b.hash;
    }

    friend bool operator!=(const CUtxo2UtxoTransaciton &a, const CUtxo2UtxoTransaciton &b)
    {
        return a.hash != b.hash;
    }

    //    std::string ToString() const;

    std::string ToString() const override;

    bool HasWitness() const
    {
        for (size_t i = 0; i < vin.size(); i++)
        {
            if (!vin[i].scriptWitness.IsNull())
            {
                return true;
            }
        }
        return false;
    }


};


#endif //SUPERBITCOIN_CUTXO2UTXOTRANSACITON_H
