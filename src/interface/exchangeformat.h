#ifndef __SBTC_EXCHANGEFORMAT_H__
#define __SBTC_EXCHANGEFORMAT_H__

enum NetNodeFlags
{
    NF_WHITELIST = 1,
    NF_OUTBOUND = 2,
    NF_DISCONNECT = 4,
    NF_MANUALCONN = 8,
    NF_ONESHOT = 16,
    NF_FEELER = 32,
    NF_RELAYTX = 64,

    NF_WITNESS = (1 << 7),
    NF_PREFERHEADERS = (1 << 8),
    NF_PREFERHEADERANDIDS = (1 << 9),
    NF_PROVIDEHEADERSANDIDS = (1 << 10),
    NF_WANTCMPCTWITNESS = (1 << 11),
    NF_DESIREDCMPCTVERSION = (1 << 12),

    NF_NEWBLOCK = (1 << 13),
    NF_NEWTRANSACTION = (1 << 14),
    NF_LASTBLOCKANNOUNCE = (1 << 15),

};

inline void SetFlagsBit(int &flags, int bit)
{
    flags |= bit;
}

inline void UnsetFlagsBit(int &flags, int bit)
{
    flags &= ~bit;
}

inline void SwitchFlagsBit(int &flags, int bit)
{
    flags ^= bit;
}

inline void InitFlagsBit(int &flags, int bit, bool bOn)
{
    if (bOn)
        SetFlagsBit(flags, bit);
    else
        UnsetFlagsBit(flags, bit);
}

inline bool IsFlagsBitOn(int flags, int bit)
{
    return flags & bit;
}


struct NodeExchangeInfo
{
    //[in]
    int64_t nodeID;
    int flags;
    int sendVersion;
    int startHeight;
    int nBlocksInFlight;
    int nLocalServices;

    //[out]
    int retFlags;
    int nMisbehavior;
    int retInteger;
    void *retPointer;
};

typedef NodeExchangeInfo ExNode;

#endif //__SBTC_EXCHANGEFORMAT_H__
