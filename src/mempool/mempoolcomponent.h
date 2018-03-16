///////////////////////////////////////////////////////////
//  mempoolcomponent.h
//  Created on:      7-3-2018 16:40:57
//  Original author: marco
///////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <log4cpp/Category.hh>
#include "util.h"
#include "interface/imempoolcomponent.h"
#include "orphantx.h"
#include "txmempool.h"

class CMempoolComponent : public ITxMempoolComponent
{
public:
    CMempoolComponent();

    ~CMempoolComponent();

    bool ComponentInitialize() override;

    bool ComponentStartup() override;

    bool ComponentShutdown() override;

    CTxMemPool &GetMemPool() override;

    bool DoesTxExist(uint256 txHash) override;

    bool NetRequestTxData(ExNode *xnode, uint256 txHash, bool witness, int64_t timeLastMempoolReq) override;

    bool NetReceiveTxData(ExNode *xnode, CDataStream &stream, uint256 &txHash) override;

    bool NetRequestTxInventory(ExNode *xnode, bool sendMempool, int64_t minFeeFilter, CBloomFilter *txFilter,
                               std::vector<uint256> &toSendTxHashes, std::vector<uint256> &haveSentTxHashes) override;

    bool RemoveOrphanTxForNode(int64_t nodeId) override;

    bool RemoveOrphanTxForBlock(const CBlock* pblock) override;

private:

    void InitializeForNet();

    CTxMemPool mempool;
    COrphanTxMgr orphanTxMgr;

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