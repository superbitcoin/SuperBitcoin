#pragma once

#include <string>
#include <vector>
#include "componentid.h"
#include "utils/uint256.h"
#include "framework/component.hpp"

class INetComponent : public appbase::TComponent<INetComponent>
{
public:
    virtual ~INetComponent() {}

    enum { ID = CID_P2P_NET };

    virtual int GetID() const override { return ID; }

    virtual bool ComponentInitialize() = 0;

    virtual bool ComponentStartup() = 0;

    virtual bool ComponentShutdown() = 0;

    virtual const char *whoru() const = 0;


    virtual bool SendNetMessage(int64_t nodeID, const std::string& command, const std::vector<unsigned char>& data) = 0;

    virtual bool BroadcastTransaction(uint256 txHash) = 0;

    //add other interface methods here ...


};

#define GET_NET_INTERFACE(ifObj) \
    auto ifObj = appbase::CBase::Instance().FindComponent<INetComponent>()
