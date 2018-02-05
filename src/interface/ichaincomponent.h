#pragma once

#include "componentid.h"
#include "exchangeformat.h"
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

    virtual int GetActiveChainHeight() const = 0;

    virtual bool NetGetCheckPoint(XNodeInfo* nodeInfo, int height) = 0;
    virtual bool NetCheckPoint(XNodeInfo* nodeInfo, CDataStream& stream) = 0;

    //add other interface methods here ...
};

#define GET_CHAIN_INTERFACE(ifObj) \
    auto ifObj = appbase::CBase::Instance().FindComponent<IChainComponent>()
