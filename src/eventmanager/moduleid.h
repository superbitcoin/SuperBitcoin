#ifndef __SBTC_MODULEID_H__
#define __SBTC_MODULEID_H__

enum SBTCModuleID
{
    MID_INVALID = 0,
    MID_DB = 2,
    MID_BLOCK_CHAIN = 4,
    MID_CONTRACT = 8,
    MID_TX_MEMPOOL = 16,
    MID_HTTP_RPC = 32,
    MID_P2P_NET = 64,
    MID_ACCOUNT = 128,
    MID_WALLET = 256,
    MID_MINER = 512,
    MID_APP = 1024,
    MID_ALL_MODULE = 0xFFFFFFFF,
};

#endif //__SBTC_MODULEID_H__