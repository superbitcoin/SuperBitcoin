///////////////////////////////////////////////////////////
//  CViewManager.h
//  Implementation of the Class CViewManager
//  Created on:      2-2-2018 16:40:58
//  Original author: marco
///////////////////////////////////////////////////////////

#ifndef __SBTC_VIEWMANAGER_H__
#define __SBTC_VIEWMANAGER_H__

#include "chain.h"
#include "coins.h"
#include "sbtccore/transaction/txdb.h"

enum DisconnectResult
{
    DISCONNECT_OK,      // All good.
    DISCONNECT_UNCLEAN, // Rolled back, but UTXO set was inconsistent with block.
    DISCONNECT_FAILED   // Something else went wrong.
};

class CViewManager
{

public:
    CViewManager();

    virtual ~CViewManager();

    bool Init(int64_t iCoinDBCacheSize, bool bReset);

    CCoinsView *getCoinViewDB();

    int ConnectBlock();

    bool ConnectBlock(const CBlock &block, const CBlockIndex *pIndex, CCoinsViewCache &viewCache);

    DisconnectResult DisconnectBlock(const CBlock &block, const CBlockIndex *pindex, CCoinsViewCache &view);

private:
    CChain cChian;
    std::unique_ptr<CCoinsViewDB> pCoinsViewDB;
    CCoinsView cCoinView;

    std::vector<uint256> getHeads();

};

#endif // !defined(__SBTC_VIEWMANAGER_H__)
