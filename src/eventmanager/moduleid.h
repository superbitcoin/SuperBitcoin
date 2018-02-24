#ifndef __SBTC_MODULEID_H__
#define __SBTC_MODULEID_H__

enum SBTCModuleID
{
    MID_INVALID     = 0,
    MID_DB          = 2,
    MID_TX_MEMPOOL  = 4,
    MID_BLOCK_CHAIN = 8,
    MID_HTTP_RPC    = 16,
    MID_P2P_NET     = 32,
    MID_ACCOUNT     = 64,
    MID_WALLET      = 128,
    MID_APP         = 512,
    MID_ALL_MODULE  = 0xFFFFFFFF,
};

#endif //__SBTC_MODULEID_H__
