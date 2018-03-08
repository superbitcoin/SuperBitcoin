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

namespace
{
    /**
     * Filter for transactions that were recently rejected by
     * AcceptToMemoryPool. These are not rerequested until the chain tip
     * changes, at which point the entire filter is reset. Protected by
     * cs_main.
     *
     * Without this filter we'd be re-requesting txs from each of our peers,
     * increasing bandwidth consumption considerably. For instance, with 100
     * peers, half of which relay a tx we don't accept, that might be a 50x
     * bandwidth increase. A flooding attacker attempting to roll-over the
     * filter using minimum-sized, 60byte, transactions might manage to send
     * 1000/sec if we have fast peers, so we pick 120,000 to give our peers a
     * two minute window to send invs to us.
     *
     * Decreasing the false positive rate is fairly cheap, so we pick one in a
     * million to make it highly unlikely for users to have issues with this
     * filter.
     *
     * Memory used: 1.3 MB
     */
    std::unique_ptr<CRollingBloomFilter> recentRejects;
    uint256 hashRecentRejectsChainTip;

    size_t vExtraTxnForCompactIt = 0;
    std::vector<std::pair<uint256, CTransactionRef>> vExtraTxnForCompact GUARDED_BY(cs_main);


    /** Relay map, protected by cs_main. */
    typedef std::map<uint256, CTransactionRef> MapRelay;

    MapRelay mapRelay;

    /** Expiration-time ordered list of (expire time, relay map entry) pairs, protected by cs_main). */
    std::deque<std::pair<int64_t, MapRelay::iterator>> vRelayExpiration;
}

class CMempoolComponent : public ITxMempoolComponent
{
public:
    CMempoolComponent();

    ~CMempoolComponent();

    bool ComponentInitialize() override;

    bool ComponentStartup() override;

    bool ComponentShutdown() override;

    bool NetRequestTxData(ExNode *xnode, uint256 txHash, bool witness, int64_t timeLastMempoolReq) override;

    bool NetReceiveTxData(ExNode *xnode, CDataStream &stream, uint256 &txHash) override;

    bool NetRequestTxInventory(ExNode *xnode, bool sendMempool, int64_t minFeeFilter, CBloomFilter *txFilter,
                               std::vector<uint256> &toSendTxHashes, std::vector<uint256> &haveSentTxHashes) override;

    void AddToCompactExtraTransactions(const CTransactionRef &tx) override;

    CTxMemPool &GetMemPool() override;

public:
    static log4cpp::Category &mlog;

private:
    CTxMemPool mempool;

    CCriticalSection cs;

    /** Dump the mempool to disk. */
    void DumpMempool();

    /** Load the mempool from disk. */
    bool LoadMempool();
};