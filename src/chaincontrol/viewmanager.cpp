///////////////////////////////////////////////////////////
//  CViewManger.cpp
//  Implementation of the Class CViewManger
//  Created on:      2-2-2018 16:40:58
//  Original author: ranger
///////////////////////////////////////////////////////////

#include "framework/base.hpp"
#include "viewmanager.h"
#include "config/argmanager.h"
#include "sbtccore/block/undo.h"

CViewManager::CViewManager()
{

}


CViewManager::~CViewManager()
{

}

bool CViewManager::Init(int64_t iCoinDBCacheSize, bool bReset)
{
    std::cout << "initialize view manager \n";

    pCoinsViewDB = std::unique_ptr<CCoinsViewDB>(new CCoinsViewDB(iCoinDBCacheSize, false, bReset));

    if (!pCoinsViewDB->Upgrade())
    {

        return false;
    }

    return true;
}

int CViewManager::ConnectBlock()
{

    return 0;
}

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  When FAILED is returned, view is left in an indeterminate state. */
DisconnectResult CViewManager::DisconnectBlock(const CBlock &block, const CBlockIndex *pindex, CCoinsViewCache &view)
{
    bool fClean = true;

    CBlockUndo blockUndo;
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull())
    {
        error("DisconnectBlock(): no undo data available");
        return DISCONNECT_FAILED;
    }
    if (!UndoReadFromDisk(blockUndo, pos, pindex->pprev->GetBlockHash()))
    {
        error("DisconnectBlock(): failure reading undo data");
        return DISCONNECT_FAILED;
    }

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size())
    {
        error("DisconnectBlock(): block and undo data inconsistent");
        return DISCONNECT_FAILED;
    }

    // undo transactions in reverse order
    for (int i = block.vtx.size() - 1; i >= 0; i--)
    {
        const CTransaction &tx = *(block.vtx[i]);
        uint256 hash = tx.GetHash();
        bool is_coinbase = tx.IsCoinBase();

        // Check that all outputs are available and match the outputs in the block itself
        // exactly.
        for (size_t o = 0; o < tx.vout.size(); o++)
        {
            if (!tx.vout[o].scriptPubKey.IsUnspendable())
            {
                COutPoint out(hash, o);
                Coin coin;
                bool is_spent = view.SpendCoin(out, &coin);
                if (!is_spent || tx.vout[o] != coin.out || pindex->nHeight != coin.nHeight ||
                    is_coinbase != coin.fCoinBase)
                {
                    fClean = false; // transaction output mismatch
                }
            }
        }

        // restore inputs
        if (i > 0)
        { // not coinbases
            CTxUndo &txundo = blockUndo.vtxundo[i - 1];
            if (txundo.vprevout.size() != tx.vin.size())
            {
                error("DisconnectBlock(): transaction and undo data inconsistent");
                return DISCONNECT_FAILED;
            }
            for (unsigned int j = tx.vin.size(); j-- > 0;)
            {
                const COutPoint &out = tx.vin[j].prevout;
                int res = ApplyTxInUndo(std::move(txundo.vprevout[j]), view, out);
                if (res == DISCONNECT_FAILED)
                    return DISCONNECT_FAILED;
                fClean = fClean && res != DISCONNECT_UNCLEAN;
            }
            // At this point, all of txundo.vprevout should have been moved out.
        }
    }

    // move best block pointer to prevout block
    view.SetBestBlock(pindex->pprev->GetBlockHash());

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

/** Apply the effects of a block on the utxo cache, ignoring that it may already have been applied. */
bool CViewManager::ConnectBlock(const CBlock &block, const CBlockIndex *pIndex, CCoinsViewCache &viewCache)
{
    // TODO: merge with ConnectBlock
    for (const CTransactionRef &tx : block.vtx)
    {
        if (!tx->IsCoinBase())
        {
            for (const CTxIn &txin : tx->vin)
            {
                viewCache.SpendCoin(txin.prevout);
            }
        }
        // Pass check = true as every addition may be an overwrite.
        AddCoins(viewCache, *tx, pIndex->nHeight, true);
    }
    return true;
}

CCoinsView *CViewManager::getCoinViewDB()
{
    assert(pCoinsViewDB);
    return pCoinsViewDB.get();
}

std::vector<uint256> CViewManager::getHeads()
{
    assert(pCoinsViewDB);
    return pCoinsViewDB->GetHeadBlocks();
};
