#ifndef __SBTC_EXCHANGEFORMAT_H__
#define __SBTC_EXCHANGEFORMAT_H__

enum NetNodeFlags
{
    NF_WHITELIST    = 1,
    NF_OUTBOUND     = 2,
    NF_DISCONNECT   = 4,
    NF_MANUALCONN   = 8,
    NF_ONESHOT      = 16,
    NF_FEELER       = 32,

    NF_WITNESS      = 64,
    NF_PREFERHEADERS = 128,
    NF_PREFERHEADERANDIDS = 256,
    NF_PROVIDEHEADERSANDIDS = (1 << 9),
    NF_WANTCMPCTWITNESS = (1 << 10),
    NF_DESIREDCMPCTVERSION = (1 << 11),

    NF_NEWBLOCK     = (1 << 12),

};

inline void SetFlagsBit(int& flags, int bit)
{
    flags |= bit;
}

inline void UnsetFlagsBit(int& flags, int bit)
{
    flags &= ~bit;
}

inline void SwitchFlagsBit(int& flags, int bit)
{
    flags ^= bit;
}

inline void InitFlagsBit(int& flags, int bit, bool bOn)
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
    int64_t     nodeID;
    int         flags;
    int         sendVersion;
    int         recvVersion;
    int         startHeight;
    int         nBlocksInFlight;
    int         serviceFlags;

    //[out]
    int         retFlags;
    int         nMisbehavior;
    int         nUnconnectingHeaders;
};

typedef NodeExchangeInfo ExNode;

#endif //__SBTC_EXCHANGEFORMAT_H__
