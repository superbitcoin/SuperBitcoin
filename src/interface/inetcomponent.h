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

    virtual bool AskForTransaction(int64_t nodeID, uint256 txHash, int flags = 0) = 0;

    virtual bool MisbehaveNode(int64_t nodeID, int num) = 0;

    //!check if the outbound target is reached
    virtual bool OutboundTargetReached(bool historicalBlockServingLimit) = 0;

    //add other interface methods here ...


};

#define GET_NET_INTERFACE(ifObj) \
    auto ifObj = appbase::CApp::Instance().FindComponent<INetComponent>()
