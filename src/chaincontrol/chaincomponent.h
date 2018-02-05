#pragma once

#include <set>
#include <map>
#include "interface/ichaincomponent.h"
#include "blockindexmanager.h"
#include "viewmanager.h"

struct database
{
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
    bool DoesBlockExist(uint256 hash) const override;
    int  GetActiveChainHeight() const override;

    bool NetGetCheckPoint(XNodeInfo* nodeInfo, int height) override;
    bool NetCheckPoint(XNodeInfo* nodeInfo, CDataStream& stream) override;
    bool NetGetBlocks(XNodeInfo* nodeInfo, CDataStream& stream, std::vector<uint256>& blockHashes) override;

private:
    database _db;
    CBlockIndexManager cIndexManager;
    CViewManager cViewManager;
    std::map<int64_t, std::set<int>> m_nodeCheckPointKnown;

    bool ReplayBlocks();
};
