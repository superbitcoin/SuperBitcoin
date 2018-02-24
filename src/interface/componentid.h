#ifndef __SBTC_COMPONENTID_H__
#define __SBTC_COMPONENTID_H__

#include "eventmanager/moduleid.h"

enum SBTCComponentID
{
    CID_INVALID     = MID_INVALID,
    CID_APP         = MID_APP,
    CID_DB          = MID_DB,
    CID_TX_MEMPOOL  = MID_TX_MEMPOOL,
    CID_BLOCK_CHAIN = MID_BLOCK_CHAIN,
    CID_HTTP_RPC    = MID_HTTP_RPC,
    CID_P2P_NET     = MID_P2P_NET,
    CID_ACCOUNT     = MID_ACCOUNT,
    CID_WALLET      = MID_WALLET,
    CID_ALL_MODULE  = MID_ALL_MODULE,
};

#endif //__SBTC_COMPONENTID_H__
