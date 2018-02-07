#pragma once

#include <vector>
#include "componentid.h"
#include "exchangeformat.h"
#include "sbtccore/streams.h"
#include "p2p/bloom.h"
#include "framework/component.hpp"

class ITxMempoolComponent : public appbase::TComponent<ITxMempoolComponent>
{
public:
    virtual ~ITxMempoolComponent() {}

    enum { ID = CID_TX_MEMPOOL };
    virtual int GetID() const override { return ID; }

    virtual bool ComponentInitialize() = 0;
    virtual bool ComponentStartup() = 0;
    virtual bool ComponentShutdown() = 0;
    virtual bool AcceptToMemoryPool(CValidationState &state, const CTransactionRef &tx, bool fLimitFree,
                            bool *pfMissingInputs, std::list<CTransactionRef> *plTxnReplaced = nullptr,
                            bool fOverrideMempoolLimit = false, const CAmount nAbsurdFee = 0) = 0;

    virtual void UpdateMempoolForReorg(DisconnectedBlockTransactions &disconnectpool, bool fAddToMempool) = 0 ;

    /** Dump the mempool to disk. */
    virtual void DumpMempool() = 0;

    /** Load the mempool from disk. */
    virtual bool LoadMempool() = 0;
    /**
     * Check if transaction will be BIP 68 final in the next block to be created.
     *
     * Simulates calling SequenceLocks() with data from the tip of the current active chain.
     * Optionally stores in LockPoints the resulting height and time calculated and the hash
     * of the block needed for calculation or skips the calculation and uses the LockPoints
     * passed in for evaluation.
     * The LockPoints should not be considered valid if CheckSequenceLocks returns false.
     *
     * See consensus/consensus.h for flag definitions.
     */
    virtual bool CheckSequenceLocks(const CTransaction &tx, int flags, LockPoints *lp = nullptr, bool useExistingLockPoints = false) = 0;


    virtual bool DoesTransactionExist(uint256 hash) = 0;

    virtual bool NetRequestTxData(ExNode* xnode, uint256 txHash, bool witness, int64_t timeLastMempoolReq) = 0;

    virtual bool NetReceiveTxData(ExNode* xnode, CDataStream& stream, uint256& txHash) = 0;

    virtual bool NetRequestTxInventory(ExNode* xnode, bool sendMempool, int64_t minFeeFilter, CBloomFilter* txFilter,
                                       std::vector<uint256>& toSendTxHashes, std::vector<uint256>& haveSentTxHashes) = 0;

    //add other interface methods here ...

};

#define GET_TXMEMPOOL_INTERFACE(ifObj) \
    auto ifObj = appbase::CBase::Instance().FindComponent<ITxMempoolComponent>()
