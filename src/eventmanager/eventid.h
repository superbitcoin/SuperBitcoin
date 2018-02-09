#ifndef __SBTC_EVENTID_H__
#define __SBTC_EVENTID_H__

enum SBTCEventID
{
    //NOTE:
    //Params: [NodeID, IsInBound, totalNodeCount]
    EID_NODE_CONNECTED  = 1,

    //NOTE:
    //Params: [NodeID, IsInBound, DisconnectReason]
    EID_NODE_DISCONNECTED = 2,

    //NOTE:
    //Params: [oldTipHash, oldChainHeight, newTipHash, newChainHeight]
    EID_CHAIN_CHANGED   = 3,


    //add other event id here...
};


#endif //__SBTC_EVENTID_H__
