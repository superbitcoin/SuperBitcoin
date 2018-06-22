///////////////////////////////////////////////////////////
//  CViewManger.cpp
//  Implementation of the Class CViewManger
//  Created on:      2-2-2018 16:40:58
//  Original author: ranger
///////////////////////////////////////////////////////////

#include <interface/ichaincomponent.h>
#include "sbtcd/baseimpl.hpp"
#include "viewmanager.h"
#include "config/argmanager.h"
#include "sbtccore/block/undo.h"
#include "blockfilemanager.h"

SET_CPP_SCOPED_LOG_CATEGORY(CID_BLOCK_CHAIN);

CViewManager &CViewManager::Instance()
{
    static CViewManager viewManager;
    return viewManager;
}

CViewManager::CViewManager()
{

}

CViewManager::~CViewManager()
{

}

int CViewManager::InitCoinsDB(int64_t iCoinDBCacheSize, bool bReset)
{
    NLogFormat("initialize view manager");

    delete pCoinsTip;
    delete pCoinsViewDB;
    delete pCoinsCatcher;

    pCoinsViewDB = new CCoinsViewDB(iCoinDBCacheSize, false, bReset);
    if (!pCoinsViewDB->Upgrade())
    {

        return false;
    }

    return true;
}

void CViewManager::InitCoinsCache()
{
    pCoinsCatcher = new CCoinsViewErrorCatcher(pCoinsViewDB);
    pCoinsTip = new CCoinsViewCache(pCoinsCatcher);
}

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  When FAILED is returned, view is left in an indeterminate state. */
DisconnectResult
CViewManager::DisconnectBlock(const CBlock &block, const CBlockIndex *pindex, CCoinsViewCache &view, bool *pfClean)
{
    if (pfClean)
        *pfClean = false;

    bool fClean = true;

    CBlockUndo blockUndo;
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull())
    {
        ELogFormat("no undo data available");
        return DISCONNECT_FAILED;
    }
    if (!UndoReadFromDisk(blockUndo, pos, pindex->pprev->GetBlockHash()))
    {
        ELogFormat("failure reading undo data");
        return DISCONNECT_FAILED;
    }

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size())
    {
        ELogFormat("block and undo data inconsistent");
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
                ELogFormat("transaction and undo data inconsistent");
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

    //sbtc-vm
    GET_CONTRACT_INTERFACE(ifContractObj);
    uint256 hashStateRoot;
    uint256 hashUTXORoot;
    CBlock prevblock;
    if (!ReadBlockFromDisk(prevblock, pindex->pprev, Params().GetConsensus())) {
        //TODO  LogError
        rLogError("ReadBlockFromDisk failed at %d, hash=%s", pindex->pprev->nHeight,
                         pindex->pprev->GetBlockHash().ToString());
    } else {
        if(prevblock.GetVMState(hashStateRoot, hashUTXORoot) == RET_VM_STATE_ERR)
        {
            ILogFormat("GetVMState err");
        }
    }
    ifContractObj->UpdateState(hashStateRoot, hashUTXORoot);
   // ifContractObj->UpdateState(pindex->pprev->hashStateRoot, pindex->pprev->hashUTXORoot);

    GET_CHAIN_INTERFACE(ifChainObj);
    if (pfClean == NULL && ifChainObj->IsLogEvents())
    {
        ifContractObj->DeleteResults(block.vtx);
        ifChainObj->GetBlockTreeDB()->EraseHeightIndex(pindex->nHeight);
    }
    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

/** Apply the effects of a block on the utxo cache, ignoring that it may already have been applied. */
bool CViewManager::ConnectBlock(const CBlock &block, const CBlockIndex *pIndex, CCoinsViewCache &viewCache)
{
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

CCoinsView *CViewManager::GetCoinViewDB()
{
    return pCoinsViewDB;
}

CCoinsViewCache *CViewManager::GetCoinsTip()
{
    return pCoinsTip;
}

std::vector<uint256> CViewManager::getHeads()
{
    assert(pCoinsViewDB);
    return pCoinsViewDB->GetHeadBlocks();
};

bool CViewManager::Flush()
{
    pCoinsTip->Flush();
    return true;
}

void CViewManager::UpdateCoins(const CTransaction &tx, CCoinsViewCache &inputs, CTxUndo &txundo, int nHeight)
{
    // mark inputs spent
    if (!tx.IsCoinBase())
    {
        txundo.vprevout.reserve(tx.vin.size());
        for (const CTxIn &txin : tx.vin)
        {
            txundo.vprevout.emplace_back();
            bool is_spent = inputs.SpendCoin(txin.prevout, &txundo.vprevout.back());
            assert(is_spent);
        }
    }
    // add outputs
    AddCoins(inputs, tx, nHeight);
}

void CViewManager::UpdateCoins(const CTransaction &tx, CCoinsViewCache &inputs, int nHeight)
{
    CTxUndo txundo;
    UpdateCoins(tx, inputs, txundo, nHeight);
}

void CViewManager::RequestShutdown()
{
    if (pCoinsViewDB)
    {
        pCoinsViewDB->RequestShutdown();
    }
}

/**
 * Restore the UTXO in a Coin at a given COutPoint
 * @param undo The Coin to be restored.
 * @param view The coins view to which to apply the changes.
 * @param out The out point that corresponds to the tx input.
 * @return A DisconnectResult as an int
 */
int CViewManager::ApplyTxInUndo(Coin &&undo, CCoinsViewCache &view, const COutPoint &out)
{
    bool fClean = true;

    if (view.HaveCoin(out))
        fClean = false; // overwriting transaction output

    if (undo.nHeight == 0)
    {
        // Missing undo metadata (height and coinbase). Older versions included this
        // information only in undo records for the last spend of a transactions'
        // outputs. This implies that it must be present for some other output of the same tx.
        const Coin &alternate = AccessByTxid(view, out.hash);
        if (!alternate.IsSpent())
        {
            undo.nHeight = alternate.nHeight;
            undo.fCoinBase = alternate.fCoinBase;
        } else
        {
            return DISCONNECT_FAILED; // adding output for transaction without known metadata
        }
    }
    // The potential_overwrite parameter to AddCoin is only allowed to be false if we know for
    // sure that the coin did not already exist in the cache. As we have queried for that above
    // using HaveCoin, we don't need to guess. When fClean is false, a coin already existed and
    // it is an overwrite.
    view.AddCoin(out, std::move(undo), !fClean);

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

