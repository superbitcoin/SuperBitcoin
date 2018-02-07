#pragma once

#include "componentid.h"
#include "framework/component.hpp"
#include "framework/base.hpp"

class ITxVerifyComponent : public appbase::TComponent<ITxVerifyComponent>
{
public:
    virtual ~ITxVerifyComponent() {}

    enum { ID = CID_TX_VERIFY };
    virtual int GetID() const override { return ID; }

    virtual bool ComponentInitialize() = 0;
    virtual bool ComponentStartup() = 0;
    virtual bool ComponentShutdown() = 0;
    //add other interface methods here ...
    /** Context-independent validity checks */
    virtual bool CheckTransaction(const CTransaction &tx, CValidationState &state, bool fCheckDuplicateInputs = true) = 0;
    /**
     * Check whether all inputs of this transaction are valid (no double spends and amounts)
     * This does not modify the UTXO set. This does not check scripts and sigs.
     * Preconditions: tx.IsCoinBase() is false.
     */
    virtual bool CheckTxInputs(const CTransaction &tx, CValidationState &state, const CCoinsViewCache &inputs, int nSpendHeight) = 0;
    /** Auxiliary functions for transaction validation (ideally should not be exposed) */

    /**
     * Count ECDSA signature operations the old-fashioned (pre-0.6) way
     * @return number of sigops this transaction's outputs will produce when spent
     * @see CTransaction::FetchInputs
     */
    virtual unsigned int GetLegacySigOpCount(const CTransaction &tx) = 0;

    /**
     * Count ECDSA signature operations in pay-to-script-hash inputs.
     *
     * @param[in] mapInputs Map of previous transactions that have outputs we're spending
     * @return maximum number of sigops required to validate this transaction's inputs
     * @see CTransaction::FetchInputs
     */
    //virtual unsigned int GetP2SHSigOpCount(const CTransaction &tx, const CCoinsViewCache &mapInputs) = 0;

    /**
     * Compute total signature operation cost of a transaction.
     * @param[in] tx     Transaction for which we are computing the cost
     * @param[in] inputs Map of previous transactions that have outputs we're spending
     * @param[out] flags Script verification flags
     * @return Total signature operation cost of tx
     */
    virtual int64_t GetTransactionSigOpCost(const CTransaction &tx, const CCoinsViewCache &inputs, int flags) = 0;

    /**
     * Check if transaction is final and can be included in a block with the
     * specified height and time. Consensus critical.
     */
    virtual bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime) = 0;

    /**
     * Calculates the block height and previous block's median time past at
     * which the transaction will be considered final in the context of BIP 68.
     * Also removes from the vector of input heights any entries which did not
     * correspond to sequence locked inputs as they do not affect the calculation.
     */
    virtual std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int> *prevHeights, const CBlockIndex &block) = 0;

    virtual bool EvaluateSequenceLocks(const CBlockIndex &block, std::pair<int, int64_t> lockPair) = 0;

    /**
     * Check if transaction is final per BIP 68 sequence numbers and can be included in a block.
     * Consensus critical. Takes as input a list of heights at which tx's inputs (in order) confirmed.
     */
    virtual bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int> *prevHeights, const CBlockIndex &block) = 0;

};

#define GET_VERIFY_INTERFACE(ifObj) \
    auto ifObj = appbase::CBase::Instance().FindComponent<ITxVerifyComponent>()
