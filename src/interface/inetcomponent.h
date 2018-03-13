#pragma once

#include <string>
#include <vector>
#include "componentid.h"
#include "utils/uint256.h"
#include "framework/component.hpp"

class CBlockIndex;
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

    virtual bool RelayCmpctBlock(const CBlockIndex *pindex, void* pcmpctblock, bool fWitnessEnabled) = 0;

    virtual bool AskForTransaction(int64_t nodeID, uint256 txHash, int flags = 0) = 0;

    virtual bool MisbehaveNode(int64_t nodeID, int num) = 0;

    //!check if the outbound target is reached
    virtual bool OutboundTargetReached(bool historicalBlockServingLimit) = 0;

    virtual int  GetNodeCount(int flags) = 0; //flags value:  1: Inbound; 2 : OutBound; 3 : Inbound + OutBound.

    virtual void UpdateBlockAvailability(int64_t nodeid, uint256 hash) = 0;

    virtual int  GetInFlightBlockCount() = 0;

    virtual bool DoseBlockInFlight(uint256 hash) = 0;

    virtual bool MarkBlockInFlight(int64_t nodeid, uint256 hash, const CBlockIndex *pindex) = 0;

    //add other interface methods here ...


};

#define GET_NET_INTERFACE(ifObj) \
    auto ifObj = GetApp()->FindComponent<INetComponent>()
