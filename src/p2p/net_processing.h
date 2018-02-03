// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_PROCESSING_H
#define BITCOIN_NET_PROCESSING_H

#include "net.h"
#include "framework/validationinterface.h"
#include "config/params.h"


/** Default number of orphan+recently-replaced txn to keep around for block reconstruction */
static const unsigned int DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN = 100;
/** Headers download timeout expressed in microseconds
 *  Timeout = base + per_header * (expected number of headers) */
static constexpr int64_t HEADERS_DOWNLOAD_TIMEOUT_BASE = 15 * 60 * 1000000; // 15 minutes
static constexpr int64_t HEADERS_DOWNLOAD_TIMEOUT_PER_HEADER = 1000; // 1ms/header
/** Protect at least this many outbound peers from disconnection due to slow behind headers chain. */
static constexpr int32_t MAX_OUTBOUND_PEERS_TO_PROTECT_FROM_DISCONNECT = 4;
/** Timeout for (unprotected) outbound peers to sync to our chainwork, in seconds */
static constexpr int64_t CHAIN_SYNC_TIMEOUT = 20 * 60; // 20 minutes
/** How frequently to check for stale tips, in seconds */
static constexpr int64_t STALE_CHECK_INTERVAL = 10 * 60; // 10 minutes
/** How frequently to check for extra outbound peers and disconnect, in seconds */
static constexpr int64_t EXTRA_PEER_CHECK_INTERVAL = 45;
/** Minimum time an outbound-peer-eviction candidate must be connected for, in order to evict, in seconds */
static constexpr int64_t MINIMUM_CONNECT_TIME = 30;


class PeerLogicValidation : public CValidationInterface, public NetEventsInterface
{
public:
    explicit PeerLogicValidation(CConnman *connman, CScheduler &scheduler);


    // Notifies listeners inherit from CValidationInterface
    void BlockConnected(const std::shared_ptr<const CBlock> &pblock, const CBlockIndex *pindexConnected,
                        const std::vector<CTransactionRef> &vtxConflicted) override;

    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override;

    void BlockChecked(const CBlock &block, const CValidationState &state) override;

    void NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock> &pblock) override;


    // Interface for message handling inherit from NetEventsInterface
    bool ProcessMessages(CNode *pfrom, std::atomic<bool> &interrupt) override;

    bool SendMessages(CNode *pto, std::atomic<bool> &interrupt) override;

    void InitializeNode(CNode *pnode) override;

    void FinalizeNode(NodeId nodeid, bool &fUpdateConnectionTime) override;



private:

    bool ProcessMessage(CNode *pfrom, const std::string &strCommand, CDataStream &vRecv,
                        int64_t nTimeReceived, const std::atomic<bool> &interruptMsgProc);

    bool ProcessRejectMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessVersionMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessVerAckMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessGetAddrMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessAddrMsg(CNode *pfrom, CDataStream &vRecv, const std::atomic<bool> &interruptMsgProc);

    bool ProcessSendHeadersMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessSendCmpctMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessPingMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessPongMsg(CNode *pfrom, CDataStream &vRecv, int64_t nTimeReceived);

    bool ProcessFilterLoadMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessFilterAddMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessFilterClearMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessFeeFilterMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessCheckPointMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessGetCheckPointMsg(CNode *pfrom, CDataStream &vRecv);



    bool ProcessMemPoolMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessGetBlocksMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessInvMsg(CNode *pfrom, CDataStream &vRecv, const std::atomic<bool> &interruptMsgProc);

    bool ProcessGetHeadersMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessHeadersMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessGetDataMsg(CNode *pfrom, CDataStream &vRecv, const std::atomic<bool> &interruptMsgProc);

    bool ProcessBlockMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessTxMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessGetBlockTxnMsg(CNode *pfrom, CDataStream &vRecv, const std::atomic<bool> &interruptMsgProc);

    bool ProcessBlockTxnMsg(CNode *pfrom, CDataStream &vRecv);

    bool ProcessCmpctBlockMsg(CNode *pfrom, CDataStream &vRecv, int64_t nTimeReceived, const std::atomic<bool> &interruptMsgProc);


    void ProcessGetData(CNode *pfrom, const std::atomic<bool> &interruptMsgProc);

    bool SendRejectsAndCheckIfBanned(CNode *pnode);

    void ConsiderEviction(CNode *pto, int64_t time_in_seconds);

    void CheckForStaleTipAndEvictPeers(const Consensus::Params &consensusParams);

    void EvictExtraOutboundPeers(int64_t time_in_seconds);


private:
    CConnman *const connman;
    int64_t m_stale_tip_check_time; //! Next time to check for stale tip
};



struct CNodeStateStats
{
    int nMisbehavior;
    int nSyncHeight;
    int nCommonHeight;
    std::vector<int> vHeightInFlight;
};

/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats);

/** Increase a node's misbehavior score. */
void Misbehaving(NodeId nodeid, int howmuch);

void AddToCompactExtraTransactions(const CTransactionRef &tx);

#endif // BITCOIN_NET_PROCESSING_H
