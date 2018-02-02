#pragma once

#include "componentid.h"
#include "framework/component.hpp"

class ITxMempoolComponent : public appbase::CComponent<ITxMempoolComponent>
{
public:
    virtual ~ITxMempoolComponent() {}

    enum { ID = CID_TX_MEMPOOL };
    virtual int GetID() const override { return ID; }

    virtual bool ComponentInitialize() = 0;
    virtual bool ComponentStartup() = 0;
    virtual bool ComponentShutdown() = 0;
    virtual const char* whoru() const = 0;

    //add other interface methods here ...

};

#define GET_TXMEMPOOL_INTERFACE(ifObj) \
    auto ifObj = appbase::CBase::Instance().FindComponent<ITxMempoolComponent>()
