#ifndef __SBTC_EXCHANGEFORMAT_H__
#define __SBTC_EXCHANGEFORMAT_H__

struct NodeExchangeInfo
{
    int64_t     nodeID;
    int         sendVersion;
    int         recvVersion;
    bool        fWhitelisted;

    int         retType;
    int         nMisbehavior;
};

typedef NodeExchangeInfo ExNode;

#endif //__SBTC_EXCHANGEFORMAT_H__
