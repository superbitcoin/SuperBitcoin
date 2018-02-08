#pragma once

#include "componentid.h"
#include "exchangeformat.h"
#include "utils/uint256.h"
#include "framework/component.hpp"

class CDataStream;
class IChainComponent : public appbase::TComponent<IChainComponent>
{
public:
    virtual ~IChainComponent() {}

    enum { ID = CID_BLOCK_CHAIN };
    virtual int GetID() const override { return ID; }

    virtual bool ComponentInitialize() = 0;
    virtual bool ComponentStartup() = 0;
    virtual bool ComponentShutdown() = 0;
    virtual const char* whoru() const = 0;

    virtual bool IsImporting() const = 0;
    virtual bool IsReindexing() const = 0;
    virtual bool IsInitialBlockDownload() const = 0;

    virtual bool DoesBlockExist(uint256 hash) = 0;
    virtual int  GetActiveChainHeight() = 0;
    virtual bool GetActiveChainTipHash(uint256& tipHash) = 0;

    virtual bool NetRequestCheckPoint(ExNode* xnode, int height) = 0;
    virtual bool NetReceiveCheckPoint(ExNode* xnode, CDataStream& stream) = 0;
    virtual bool NetRequestBlocks(ExNode* xnode, CDataStream& stream, std::vector<uint256>& blockHashes) = 0;
    virtual bool NetRequestHeaders(ExNode* xnode, CDataStream& stream) = 0;
    virtual bool NetReceiveHeaders(ExNode* xnode, CDataStream& stream) = 0;
    virtual bool NetRequestBlockData(ExNode* xnode, uint256 blockHash, int blockType) = 0;
    virtual bool NetReceiveBlockData(ExNode* xnode, CDataStream& stream, uint256& blockHash) = 0;
    virtual bool NetRequestBlockTxn(ExNode* xnode, CDataStream& stream) = 0;

    //add other interface methods here ...
};

#define GET_CHAIN_INTERFACE(ifObj) \
    auto ifObj = appbase::CBase::Instance().FindComponent<IChainComponent>()
