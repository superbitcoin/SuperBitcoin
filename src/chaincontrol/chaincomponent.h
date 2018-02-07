#pragma once

#include <set>
#include <map>
#include "interface/ichaincomponent.h"
#include "blockfilemanager.h"
#include "blockindexmanager.h"
#include "viewmanager.h"

struct database
{
};

enum FlushStateMode
{
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
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

    int  GetActiveChainHeight() override;


    // P2P network message response.
    bool NetRequestCheckPoint(ExNode* xnode, int height) override;
    bool NetReceiveCheckPoint(ExNode* xnode, CDataStream& stream) override;
    bool NetRequestBlocks(ExNode* xnode, CDataStream& stream, std::vector<uint256>& blockHashes) override;
    bool NetRequestHeaders(ExNode* xnode, CDataStream& stream) override;
    bool NetReceiveHeaders(ExNode* xnode, CDataStream& stream) override;
    bool NetReceiveBlock(ExNode* xnode, CDataStream& stream, uint256& blockHash) override;

private:
    database _db;
    CBlockFileManager cFileManager;
    CBlockIndexManager cIndexManager;
    CViewManager cViewManager;
    std::map<int64_t, std::set<int>> m_nodeCheckPointKnown;


    bool ReplayBlocks();

    CBlockIndex *Tip();

    void SetTip(CBlockIndex *pIndexTip);

    bool NeedFullFlush(FlushStateMode mode);

    bool DisconnectTip(CValidationState &state);

    bool ActivateBestChainStep(CValidationState &state, CBlockIndex *pindexMostWork,
                               const std::shared_ptr<const CBlock> &pblock, bool &fInvalidFound);

    bool ActivateBestChain(CValidationState &state, std::shared_ptr<const CBlock> pblock);

    bool FlushStateToDisk(CValidationState &state, FlushStateMode mode);

    bool NetReceiveHeaders(ExNode* xnode, const std::vector<CBlockHeader>& headers);
};
