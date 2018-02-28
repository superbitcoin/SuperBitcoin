#pragma once

#include <memory>
#include "interface/inetcomponent.h"
#include "net.h"

class PeerLogicValidation;
class CNetComponent : public INetComponent
{
public:
    CNetComponent();
    ~CNetComponent();

    bool ComponentInitialize() override;
    bool ComponentStartup() override;
    bool ComponentShutdown() override;
    const char* whoru() const override { return "I am CNetComponent\n";}

    log4cpp::Category &getLog() override;

    bool SendNetMessage(int64_t nodeID, const std::string& command, const std::vector<unsigned char>& data) override;

    bool BroadcastTransaction(uint256 txHash) override;

    bool AskForTransaction(int64_t nodeID, uint256 txHash, int flags) override;

    bool MisbehaveNode(int64_t nodeID, int num) override;

    bool OutboundTargetReached(bool historicalBlockServingLimit) override;

    int  GetNodeCount(int flags) override;


private:
    log4cpp::Category &mlog;

    std::unique_ptr<CConnman>   netConnMgr;
    std::unique_ptr<PeerLogicValidation> peerLogic;

    CConnman::Options  netConnOptions;
};