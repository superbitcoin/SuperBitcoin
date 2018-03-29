#pragma once

#include <vector>
#include "base/base.hpp"
#include "componentid.h"
#include "exchangeformat.h"
#include "sbtccore/streams.h"
#include "p2p/bloom.h"
#include "framework/component.hpp"

class CBlock;

class IMinerComponent : public appbase::TComponent<IMinerComponent>
{
public:
    virtual ~IMinerComponent()
    {
    }

    enum
    {
        ID = CID_MINER
    };

    virtual int GetID() const override
    {
        return ID;
    }

    virtual bool ComponentInitialize() = 0;

    virtual bool ComponentStartup() = 0;

    virtual bool ComponentShutdown() = 0;

    //add other interface methods here ...

};

#define GET_MINER_INTERFACE(ifObj) \
    auto ifObj = GetApp()->FindComponent<IMinerComponent>()
