#pragma once

#include "componentid.h"
#include "../framework/component.hpp"

using namespace appbase;
class IWalletComponent : public CComponent<IWalletComponent>
{
public:
    virtual ~IWalletComponent() {}

    enum { ID = CID_WALLET };
    virtual int GetID() const override { return ID; }

    virtual bool ComponentInitialize() = 0;
    virtual bool ComponentStartup() = 0;
    virtual bool ComponentShutdown() = 0;
    virtual const char* whoru() const = 0;

    //add other interface methods here ...

};

