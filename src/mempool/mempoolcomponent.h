///////////////////////////////////////////////////////////
//  mempoolcomponent.h
//  Created on:      7-3-2018 16:40:57
//  Original author: marco
///////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <log4cpp/Category.hh>
#include "utils/util.h"
#include "interface/imempoolcomponent.h"
#include "txmempool.h"

class CMempoolComponent : public ITxMempoolComponent
{
public:
    CMempoolComponent();

    ~CMempoolComponent();

    bool ComponentInitialize() override;

    bool ComponentStartup() override;

    bool ComponentShutdown() override;

    bool DoesTxExist(uint256 txHash, uint256 tipBlockHash) override;

    bool NetRequestTxData(ExNode *xnode, uint256 txHash, bool witness, int64_t timeLastMempoolReq) override;

    bool NetReceiveTxData(ExNode *xnode, CDataStream &stream, uint256 &txHash) override;

    bool NetRequestTxInventory(ExNode *xnode, bool sendMempool, int64_t minFeeFilter, CBloomFilter *txFilter,
                               std::vector<uint256> &toSendTxHashes, std::vector<uint256> &haveSentTxHashes) override;

    CTxMemPool &GetMemPool() override;

public:
    static log4cpp::Category &mlog;

private:

    void InitializeForNet();

    CTxMemPool mempool;

    CCriticalSection cs;

    bool bFeeEstimatesInitialized = false;

    bool bDumpMempoolLater = false;

    /** Dump the mempool to disk. */
    void DumpMempool();

    /** Load the mempool from disk. */
    bool LoadMempool();

    void InitFeeEstimate();

    void FlushFeeEstimate();

};