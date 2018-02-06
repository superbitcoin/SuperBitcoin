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

    virtual bool NetGetCheckPoint(ExNode* xnode, int height) = 0;
    virtual bool NetCheckPoint(ExNode* xnode, CDataStream& stream) = 0;
    virtual bool NetGetBlocks(ExNode* xnode, CDataStream& stream, std::vector<uint256>& blockHashes) = 0;
    virtual bool NetGetHeaders(ExNode* xnode, CDataStream& stream) = 0;

    //add other interface methods here ...
};

#define GET_CHAIN_INTERFACE(ifObj) \
    auto ifObj = appbase::CBase::Instance().FindComponent<IChainComponent>()
