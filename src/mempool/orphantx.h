//
// Created by root1 on 18-2-3.
//

#ifndef SUPERBITCOIN_ORPHANTX_H
#define SUPERBITCOIN_ORPHANTX_H

#include "sbtccore/transaction/transaction.h"
#include "p2p/net.h"

/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** Expiration time for orphan transactions in seconds */
static const int64_t ORPHAN_TX_EXPIRE_TIME = 20 * 60;
/** Minimum time between orphan transactions expire time checks in seconds */
static const int64_t ORPHAN_TX_EXPIRE_INTERVAL = 5 * 60;

struct IteratorComparator
{
    template<typename I>
    bool operator()(const I &a, const I &b)
    {
        return &(*a) < &(*b);
    }
};

struct _OrphanTx
{
    CTransactionRef tx;
    NodeId fromPeer;
    int64_t nTimeExpire;
};

class COrphanTx
{
public:
    static COrphanTx &Instance();
    bool AddOrphanTx(const CTransactionRef &tx, NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    int EraseOrphanTx(uint256 hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void EraseOrphansFor(NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void Clear() GUARDED_BY(cs_main);
    bool Exists(uint256 hash) GUARDED_BY(cs_main);

private:
    COrphanTx();
    ~COrphanTx();
public:
    std::map<uint256, _OrphanTx> m_mapOrphanTransactions GUARDED_BY(cs_main);
    std::map<COutPoint, std::set<std::map<uint256, _OrphanTx>::iterator, IteratorComparator>> m_mapOrphanTransactionsByPrev GUARDED_BY(cs_main);

};
#endif //SUPERBITCOIN_ORPHANTX_H
