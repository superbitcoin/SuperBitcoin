#ifndef __SBTC_EXCHANGEFORMAT_H__
#define __SBTC_EXCHANGEFORMAT_H__

struct NodeExchangeInfo
{
    //[in]
    int64_t     nodeID;
    int         sendVersion;
    int         recvVersion;
    bool        fWhitelisted;
    bool        fDisconnect;
    bool        fOutBound;
    bool        fManualConn;

    //[out]
    int         retType;
    int         nMisbehavior;
    int         nUnconnectingHeaders;
};

typedef NodeExchangeInfo ExNode;

#endif //__SBTC_EXCHANGEFORMAT_H__
