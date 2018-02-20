#pragma once

#include <set>
#include <map>
#include <log4cpp/Category.hh>
#include "interface/ichaincomponent.h"
#include "blockfilemanager.h"
#include "blockindexmanager.h"
#include "viewmanager.h"
#include "mempool/txmempool.h"
#include "sbtccore/block/blockencodings.h"

struct database
{
};

struct PerBlockConnectTrace
{
    CBlockIndex *pindex = nullptr;
    std::shared_ptr<const CBlock> pblock;
    std::shared_ptr<std::vector<CTransactionRef>> conflictedTxs;

    PerBlockConnectTrace() : conflictedTxs(std::make_shared<std::vector<CTransactionRef>>())
    {
    }
};

/**
 * Used to track blocks whose transactions were applied to the UTXO state as a
 * part of a single ActivateBestChainStep call.
 *
 * This class also tracks transactions that are removed from the mempool as
 * conflicts (per block) and can be used to pass all those transactions
 * through SyncTransaction.
 *
 * This class assumes (and asserts) that the conflicted transactions for a given
 * block are added via mempool callbacks prior to the BlockConnected() associated
 * with those transactions. If any transactions are marked conflicted, it is
 * assumed that an associated block will always be added.
 *
 * This class is single-use, once you call GetBlocksConnected() you have to throw
 * it away and make a new one.
 */
class ConnectTrace
{
private:
    std::vector<PerBlockConnectTrace> blocksConnected;
    CTxMemPool &pool;

public:
    ConnectTrace(CTxMemPool &_pool) : blocksConnected(1), pool(_pool)
    {
        pool.NotifyEntryRemoved.connect(boost::bind(&ConnectTrace::NotifyEntryRemoved, this, _1, _2));
    }

    ~ConnectTrace()
    {
        pool.NotifyEntryRemoved.disconnect(boost::bind(&ConnectTrace::NotifyEntryRemoved, this, _1, _2));
    }

    void BlockConnected(CBlockIndex *pindex, std::shared_ptr<const CBlock> pblock)
    {
        assert(!blocksConnected.back().pindex);
        assert(pindex);
        assert(pblock);
        blocksConnected.back().pindex = pindex;
        blocksConnected.back().pblock = std::move(pblock);
        blocksConnected.emplace_back();
    }

    std::vector<PerBlockConnectTrace> &GetBlocksConnected()
    {
        // We always keep one extra block at the end of our list because
        // blocks are added after all the conflicted transactions have
        // been filled in. Thus, the last entry should always be an empty
        // one waiting for the transactions from the next block. We pop
        // the last entry here to make sure the list we return is sane.
        assert(!blocksConnected.back().pindex);
        assert(blocksConnected.back().conflictedTxs->empty());
        blocksConnected.pop_back();
        return blocksConnected;
    }

    void NotifyEntryRemoved(CTransactionRef txRemoved, MemPoolRemovalReason reason)
    {
        assert(!blocksConnected.back().pindex);
        if (reason == MemPoolRemovalReason::CONFLICT)
        {
            blocksConnected.back().conflictedTxs->emplace_back(std::move(txRemoved));
        }
    }
};

/**
 * Threshold condition checker that triggers when unknown versionbits are seen on the network.
 */
class WarningBitsConditionChecker : public AbstractThresholdConditionChecker
{
private:
    int bit;

public:
    WarningBitsConditionChecker(int bitIn) : bit(bitIn)
    {
    }

    int64_t BeginTime(const Consensus::Params &params) const override
    {
        return 0;
    }

    int64_t EndTime(const Consensus::Params &params) const override
    {
        return std::numeric_limits<int64_t>::max();
    }

    int Period(const Consensus::Params &params) const override
    {
        return params.nMinerConfirmationWindow;
    }

    int Threshold(const Consensus::Params &params) const override
    {
        return params.nRuleChangeActivationThreshold;
    }

    bool Condition(const CBlockIndex *pindex, const Consensus::Params &params) const override
    {
        return ((pindex->nVersion & VERSIONBITS_TOP_MASK) == VERSIONBITS_TOP_BITS) &&
               ((pindex->nVersion >> bit) & 1) != 0 &&
               ((ComputeBlockVersion(pindex->pprev, params) >> bit) & 1) == 0;
    }
};

// Protected by cs
static ThresholdConditionCache warningcache[VERSIONBITS_NUM_BITS];

enum FlushStateMode
{
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};

enum ResultChainControl
{
    OK_CHAIN = 0,
    ERR_FUTURE_BLOCK = -1000,
    ERR_VERIFY_DB,
};

class CChainCommonent : public IChainComponent
{
public:
    CChainCommonent();

    ~CChainCommonent();

    bool ComponentInitialize() override;

    bool ComponentStartup() override;

    bool ComponentShutdown() override;

    database &db()
    {
        return _db;
    }

    const char *whoru() const override
    {
        return "I am CChainCommonent\n";
    }

    bool IsImporting() const override;

    bool IsReindexing() const override;

    bool IsInitialBlockDownload() const override;

    bool DoesBlockExist(uint256 hash) override;

    int GetActiveChainHeight() override;

    bool GetActiveChainTipHash(uint256 &tipHash) override;


    // P2P network message response.
    bool NetRequestCheckPoint(ExNode *xnode, int height) override;

    bool NetReceiveCheckPoint(ExNode *xnode, CDataStream &stream) override;

    bool NetRequestBlocks(ExNode *xnode, CDataStream &stream, std::vector<uint256> &blockHashes) override;

    bool NetRequestHeaders(ExNode *xnode, CDataStream &stream) override;

    bool NetReceiveHeaders(ExNode *xnode, CDataStream &stream) override;

    bool NetRequestBlockData(ExNode *xnode, uint256 blockHash, int blockType) override;

    bool NetReceiveBlockData(ExNode *xnode, CDataStream &stream, uint256 &blockHash) override;

    bool NetRequestBlockTxn(ExNode *xnode, CDataStream &stream) override;

private:

    bool NetReceiveHeaders(ExNode *xnode, const std::vector<CBlockHeader> &headers);

    bool NetSendBlockTransactions(ExNode *xnode, const BlockTransactionsRequest &req, const CBlock &block);

    void OnNodeDisconnected(int64_t nodeID, bool bInBound, int disconnectReason);

private:
    database _db;
    bool bReIndex;
    bool bRequestShutdown;
    CBlockIndexManager cIndexManager;
    CViewManager cViewManager;

    CCriticalSection cs_xnodeGuard;
    std::map<int64_t, std::set<int>> m_nodeCheckPointKnown;
    CCriticalSection cs;

    bool ReplayBlocks();

    CBlockIndex *Tip();

    void SetTip(CBlockIndex *pIndexTip);

    bool NeedFullFlush(FlushStateMode mode);

    bool IsInitialBlockDownload();

    void CheckForkWarningConditions();

    void DoWarning(const std::string &strWarning);

    void ThreadScriptCheck();

    bool ConnectBlock(const CBlock &block, CValidationState &state, CBlockIndex *pIndex, CCoinsViewCache &view,
                      const CChainParams &chainparams, bool fJustCheck = false);

    bool LoadChainTip(const CChainParams &chainparams);

    void UpdateTip(CBlockIndex *pindexNew, const CChainParams &chainParams);

    bool ConnectTip(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pIndexNew,
                    const std::shared_ptr<const CBlock> &pblock, ConnectTrace &connectTrace,
                    DisconnectedBlockTransactions &disconnectpoo);

    bool DisconnectTip(CValidationState &state, const CChainParams &chainparams,
                       DisconnectedBlockTransactions *disconnectpool);

    bool ActivateBestChainStep(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pindexMostWork,
                               const std::shared_ptr<const CBlock> &pblock, bool &fInvalidFound,
                               ConnectTrace &connectTrace);

    bool
    ActivateBestChain(CValidationState &state, const CChainParams &chainparams, std::shared_ptr<const CBlock> pblock);

    bool FlushStateToDisk(CValidationState &state, FlushStateMode mode, const CChainParams &chainparams);

    bool InvalidateBlock(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pindex);

    bool CheckActiveChain(CValidationState &state, const CChainParams &chainparams);

    bool RewindBlock(const CChainParams &params);

    bool
    CheckBlock(const CBlock &block, CValidationState &state, const Consensus::Params &consensusParams, bool fCheckPOW,
               bool fCheckMerkleRoot);

    bool ContextualCheckBlock(const CBlock &block, CValidationState &state, const Consensus::Params &consensusParams,
                              const CBlockIndex *pindexPrev);

    bool LoadGenesisBlock(const CChainParams &chainparams);

    int VerifyBlocks();

    bool ThreadImport();

    bool LoadExternalBlockFile(const CChainParams &chainparams, FILE *fileIn, CDiskBlockPos *dbp);

    bool
    AcceptBlock(const std::shared_ptr<const CBlock> &pblock, CValidationState &state, const CChainParams &chainparams,
                CBlockIndex **ppindex, bool fRequested, const CDiskBlockPos *dbp, bool *fNewBlock);

    void NotifyHeaderTip();

public:
    static log4cpp::Category & mlog;


};
