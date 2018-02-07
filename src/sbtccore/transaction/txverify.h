//
// Created by root1 on 18-2-6.
//

#ifndef SUPERBITCOIN_TXVERIFY_H
#define SUPERBITCOIN_TXVERIFY_H

#include "interface/ITxVerifyComponent.h"
#include "transaction/transaction.h"
#include "chaincontrol/validation.h"
#include "chaincontrol/coins.h"
#include "chaincontrol/chain.h"
#include <stdint.h>
#include <vector>
class CTxVerify : public ITxVerifyComponent
{
public:
    bool ComponentInitialize() override;
    bool ComponentStartup() override;
    bool ComponentShutdown() override;
    /** Transaction validation functions */

    /** Context-independent validity checks */
    virtual bool CheckTransaction(const CTransaction &tx, CValidationState &state, bool fCheckDuplicateInputs = true);

//    namespace Consensus
//    {
        /**
         * Check whether all inputs of this transaction are valid (no double spends and amounts)
         * This does not modify the UTXO set. This does not check scripts and sigs.
         * Preconditions: tx.IsCoinBase() is false.
         */
        bool CheckTxInputs(const CTransaction &tx, CValidationState &state, const CCoinsViewCache &inputs, int nSpendHeight);
    //} // namespace Consensus

    /** Auxiliary functions for transaction validation (ideally should not be exposed) */

    /**
     * Count ECDSA signature operations the old-fashioned (pre-0.6) way
     * @return number of sigops this transaction's outputs will produce when spent
     * @see CTransaction::FetchInputs
     */
    virtual unsigned int GetLegacySigOpCount(const CTransaction &tx);

    /**
     * Count ECDSA signature operations in pay-to-script-hash inputs.
     *
     * @param[in] mapInputs Map of previous transactions that have outputs we're spending
     * @return maximum number of sigops required to validate this transaction's inputs
     * @see CTransaction::FetchInputs
     */
    unsigned int GetP2SHSigOpCount(const CTransaction &tx, const CCoinsViewCache &mapInputs);

    /**
     * Compute total signature operation cost of a transaction.
     * @param[in] tx     Transaction for which we are computing the cost
     * @param[in] inputs Map of previous transactions that have outputs we're spending
     * @param[out] flags Script verification flags
     * @return Total signature operation cost of tx
     */
    virtual int64_t GetTransactionSigOpCost(const CTransaction &tx, const CCoinsViewCache &inputs, int flags);

    /**
     * Check if transaction is final and can be included in a block with the
     * specified height and time. Consensus critical.
     */
    virtual bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime);

    /**
     * Calculates the block height and previous block's median time past at
     * which the transaction will be considered final in the context of BIP 68.
     * Also removes from the vector of input heights any entries which did not
     * correspond to sequence locked inputs as they do not affect the calculation.
     */
    virtual std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int> *prevHeights, const CBlockIndex &block);

    virtual bool EvaluateSequenceLocks(const CBlockIndex &block, std::pair<int, int64_t> lockPair);

    /**
     * Check if transaction is final per BIP 68 sequence numbers and can be included in a block.
     * Consensus critical. Takes as input a list of heights at which tx's inputs (in order) confirmed.
     */
    virtual bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int> *prevHeights, const CBlockIndex &block);

private:
    CTxVerify();
    ~CTxVerify();
};
#endif //SUPERBITCOIN_TXVERIFY_H
