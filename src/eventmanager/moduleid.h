#ifndef __SBTC_MODULEID_H__
#define __SBTC_MODULEID_H__

enum SBTCModuleID
{
    MID_INVALID     = 0,
    MID_TX_MEMPOOL  = 1,
    MID_BLOCK_CHAIN = 2,
    MID_HTTP_RPC    = 4,
    MID_P2P_NET     = 8,
    MID_MAIN_FRAME  = 16,
    MID_WALLET      = 32,
    MID_DB          = 64,
    MID_ACCOUNT     = 128,
    MID_ALL_MODULE  = 0xFFFFFFFF,
};

#endif //__SBTC_MODULEID_H__
