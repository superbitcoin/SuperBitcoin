#pragma once

#include <memory>
#include "interface/inetcomponent.h"
#include "net.h"

#if ENABLE_ZMQ
#include "zmq/zmqnotificationinterface.h"
#endif

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


    bool SendNetMessage(int64_t nodeID, const std::string& command, const std::vector<unsigned char>& data) override;

    bool BroadcastTransaction(uint256 txHash) override;

    bool RelayCmpctBlock(const CBlockIndex *pindex, void* pcmpctblock, bool fWitnessEnabled) override;

    bool AskForTransaction(int64_t nodeID, uint256 txHash, int flags) override;

    bool AddTxInventoryKnown(int64_t nodeID, uint256 txHash, int flags) override;

    bool MisbehaveNode(int64_t nodeID, int num) override;

    bool OutboundTargetReached(bool historicalBlockServingLimit) override;

    int  GetNodeCount(int flags) override;

    void UpdateBlockAvailability(int64_t nodeid, uint256 hash) override;

    int  GetInFlightBlockCount() override;

    bool DoseBlockInFlight(uint256 hash) override;

    bool MarkBlockInFlight(int64_t nodeid, uint256 hash, const CBlockIndex *pindex) override;


private:

    std::unique_ptr<CConnman>   netConnMgr;
    std::unique_ptr<PeerLogicValidation> peerLogic;

    CZMQNotificationInterface *pzmqNotificationInterface;

    CConnman::Options  netConnOptions;
};

