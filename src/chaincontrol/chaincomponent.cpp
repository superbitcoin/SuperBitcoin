#include <iostream>
#include<thread>      //std::thread

#include "chaincomponent.h"
#include "sbtccore/streams.h"
#include "interface/inetcomponent.h"
#include "utils/net/netmessagehelper.h"
#include "sbtccore/block/validation.h"
#include "utils/reverse_iterator.h"
#include "interface/imempoolcomponent.h"
#include "framework/warnings.h"
#include "utils/util.h"
#include "sbtccore/block/merkle.h"
#include "sbtccore/checkqueue.h"
#include "sbtccore/block/undo.h"
#include "utils/merkleblock.h"
#include "framework/base.hpp"
#include "eventmanager/eventmanager.h"

static int64_t nTimeCheck = 0;
static int64_t nTimeForks = 0;
static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;
static int64_t nTimeTotal = 0;
static CCheckQueue<CScriptCheck> scriptcheckqueue(128);

void CChainCommonent::ThreadScriptCheck()
{
    RenameThread("bitcoin-scriptch");
    scriptcheckqueue.Thread();
}

CChainCommonent::CChainCommonent()
{
}

CChainCommonent::~CChainCommonent()
{
}

bool CChainCommonent::ComponentInitialize()
{
    std::cout << "initialize chain component \n";


    //    GET_BASE_INTERFACE(ifBaseObj);
    //    assert(ifBaseObj != nullptr);

    app().GetEventManager().RegisterEventHandler(EID_NODE_DISCONNECTED, this, &CChainCommonent::OnNodeDisconnected);


    const CChainParams &chainParams = app().GetChainParams();
    const CArgsManager &cArgs = app().GetArgsManager();

    LoadCheckPoint();

    // block pruning; get the amount of disk space (in MiB) to allot for block & undo files
    int64_t iPruneArg = cArgs.GetArg<int32_t>("-prune", 0);
    if (iPruneArg < 0)
    {
        return InitError(_("Prune cannot be configured with a negative value."));
    }
    nPruneTarget = (uint64_t)iPruneArg * 1024 * 1024;
    if (iPruneArg == 1)
    {  // manual pruning: -prune=1
        mlog.debug(
                "Block pruning enabled.  Use RPC call pruneblockchain(height) to manually prune block and undo files.\n");
        nPruneTarget = std::numeric_limits<uint64_t>::max();
        fPruneMode = true;
    } else if (nPruneTarget)
    {
        if (nPruneTarget < MIN_DISK_SPACE_FOR_BLOCK_FILES)
        {
            return InitError(strprintf(_("Prune configured below the minimum of %d MiB.  Please use a higher number."),
                                       MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024));
        }
        mlog.debug("Prune configured to target %uMiB on disk for block and undo files.\n", nPruneTarget / 1024 / 1024);
        fPruneMode = true;
    }

    bool bArgReIndex = cArgs.GetArg<bool>("-reindex", false);
    bool bReindexChainState = cArgs.GetArg<bool>("-reindex-chainstate", false);

    // cache size calculations
    int64_t iTotalCache = (cArgs.GetArg<int64_t>("-dbcache", nDefaultDbCache) << 20);
    iTotalCache = std::max(iTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    iTotalCache = std::min(iTotalCache, nMaxDbCache << 20); // total cache cannot be greater than nMaxDbcache
    int64_t iBlockTreeDBCache = iTotalCache / 8;
    iBlockTreeDBCache = std::min(iBlockTreeDBCache,
                                 (cArgs.GetArg<bool>("-txindex", DEFAULT_TXINDEX) ? nMaxBlockDBAndTxIndexCache
                                                                                  : nMaxBlockDBCache) << 20);
    iTotalCache -= iBlockTreeDBCache;
    int64_t iCoinDBCache = std::min(iTotalCache / 2,
                                    (iTotalCache / 4) + (1 << 23)); // use 25%-50% of the remainder for disk cache
    iCoinDBCache = std::min(iCoinDBCache, nMaxCoinsDBCache << 20); // cap total coins db cache
    iTotalCache -= iCoinDBCache;
    mlog.debug("Cache configuration:\n");
    mlog.debug("* Using %.1fMiB for block index database\n", iBlockTreeDBCache * (1.0 / 1024 / 1024));
    mlog.debug("* Using %.1fMiB for chain state database\n", iCoinDBCache * (1.0 / 1024 / 1024));

    int64_t iStart;
    bool bLoaded = false;
    while (!bLoaded && !bRequestShutdown)
    {
        bool bReIndex = bArgReIndex;
        std::string strLoadError;

        uiInterface.InitMessage(_("Loading block index..."));

        iStart = GetTimeMillis();

        do
        {
            if (bReIndex && fPruneMode)
            {
                CleanupBlockRevFiles();
            }

            if (bRequestShutdown)
            {
                break;
            }

            int ret = cIndexManager.LoadBlockIndex(iBlockTreeDBCache, bReIndex, chainParams);
            if (ret == OK_BLOCK_INDEX)
            {
                strLoadError = _("Error loading block database");
                break;
            } else if (ret == ERR_LOAD_GENESIS)
            {
                return InitError(_("Incorrect or no genesis block found. Wrong datadir for network?"));
            } else if (ret == ERR_TXINDEX_STATE)
            {
                strLoadError = _("You need to rebuild the database using -reindex to change -txindex");
                break;
            } else if (ret == ERR_PRUNE_STATE)
            {
                strLoadError = _(
                        "You need to rebuild the database using -reindex to go back to unpruned mode.  This will redownload the entire blockchain");
                break;
            }

            if (cIndexManager.NeedInitGenesisBlock(chainParams))
            {
                if (!LoadGenesisBlock(chainParams))
                {
                    strLoadError = _("Error initializing block database");
                    break;
                }
            }

            uiInterface.InitMessage(_("Init View..."));
            ret = cViewManager.InitCoinsDB(iCoinDBCache, bReIndex | bReindexChainState);
            if (ret == ERR_VIEW_UPGRADE)
            {
                strLoadError = _("Error upgrading chainstate database");
                break;
            }

            // ReplayBlocks is a no-op if we cleared the coinsviewdb with -reindex or -reindex-chainstate
            if (!ReplayBlocks())
            {
                strLoadError = _(
                        "Unable to replay blocks. You will need to rebuild the database using -reindex-chainstate.");
                break;
            }

            // The on-disk coinsdb is now in a good state, create the cache
            cViewManager.InitCoinsCache();

            bool bCoinsViewEmpty =
                    bReIndex || bReindexChainState || cViewManager.GetCoinsTip()->GetBestBlock().IsNull();
            if (!bCoinsViewEmpty)
            {
                // LoadChainTip sets chainActive based on pcoinsTip's best block
                if (!LoadChainTip(chainParams))
                {
                    strLoadError = _("Error initializing block database");
                    break;
                }
                // check current chain according to checkpoint
                CValidationState state;
                CheckActiveChain(state, chainParams);
                assert(state.IsValid());
                assert(Tip() != nullptr);
            }

            if (!bReIndex)
            {
                // Note that RewindBlockIndex MUST run even if we're about to -reindex-chainstate.
                // It both disconnects blocks based on chainActive, and drops block data in
                // mapBlockIndex based on lack of available witness data.
                uiInterface.InitMessage(_("Rewinding blocks..."));
                if (!RewindBlock(chainParams))
                {
                    strLoadError = _(
                            "Unable to rewind the database to a pre-fork state. You will need to redownload the blockchain");
                    break;
                }
            }

            if (!bCoinsViewEmpty)
            {
                ret = VerifyBlocks();
                if (ret == OK_CHAIN)
                {

                } else if (ret == ERR_FUTURE_BLOCK)
                {
                    strLoadError = _("The block database contains a block which appears to be from the future. "
                                             "This may be due to your computer's date and time being set incorrectly. "
                                             "Only rebuild the block database if you are sure that your computer's date and time are correct");
                    break;
                } else if (ret == ERR_VERIFY_DB)
                {
                    strLoadError = _("Corrupted block database detected");
                    break;
                }
            }

            bLoaded = true;
        } while (false);

        if (!bLoaded && !bRequestShutdown)
        {
            // first suggest a reindex
            if (!bReIndex)
            {
                bool bRet = uiInterface.ThreadSafeQuestion(
                        strLoadError + ".\n\n" + _("Do you want to rebuild the block database now?"),
                        strLoadError + ".\nPlease restart with -reindex or -reindex-chainstate to recover.",
                        "", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
                if (bRet)
                {
                    bArgReIndex = true;
                    bRequestShutdown = false;
                } else
                {
                    mlog.fatal("Aborted block database rebuild. Exiting.\n");
                    return false;
                }
            } else
            {
                return InitError(strLoadError);
            }
        }
    }

    // As LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (bRequestShutdown)
    {
        mlog.debug("Shutdown requested. Exiting.\n");
        return false;
    }
    if (bLoaded)
    {
        mlog.debug(" block index %15dms\n", GetTimeMillis() - iStart);
    }
    return true;
}

bool CChainCommonent::ComponentStartup()
{
    std::cout << "startup chain component \n";
    bRequestShutdown = false;

    std::thread t(std::bind(&CChainCommonent::ThreadImport, this));
    return true;
}

bool CChainCommonent::ComponentShutdown()
{
    std::cout << "shutdown chain component \n";
    bRequestShutdown = true;
    return true;
}

bool CChainCommonent::IsImporting() const
{
    //TODO:
    return fImporting;
}

bool CChainCommonent::IsReindexing() const
{
    //TODO:
    return fReindex;
}

bool CChainCommonent::IsInitialBlockDownload() const
{
    //TODO:
    return IsInitialBlockDownload();
}

bool CChainCommonent::DoesBlockExist(uint256 hash)
{
    //TODO:
    return cIndexManager.GetBlockIndex(hash);
}

bool CChainCommonent::LoadGenesisBlock(const CChainParams &chainparams)
{
    LOCK(cs);

    try
    {
        CBlock &block = const_cast<CBlock &>(chainparams.GenesisBlock());
        // Start new block file
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        CValidationState state;
        if (!cIndexManager.FindBlockPos(state, blockPos, nBlockSize + 8, 0, block.GetBlockTime()))
            return error("%s: FindBlockPos failed", __func__);
        if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
            return error("%s: writing genesis block to disk failed", __func__);
        CBlockIndex *pindex = cIndexManager.AddToBlockIndex(block);
        if (!cIndexManager.ReceivedBlockTransactions(block, state, pindex, blockPos, chainparams.GetConsensus()))
            return error("%s: genesis block not accepted", __func__);
    } catch (const std::runtime_error &e)
    {
        return error("%s: failed to write genesis block: %s", __func__, e.what());
    }

    return true;
}

int CChainCommonent::GetActiveChainHeight()
{
    //TODO:
    return cIndexManager.GetChain().Height();
}

bool CChainCommonent::GetActiveChainTipHash(uint256 &tipHash)
{
    if (CBlockIndex *tip = cIndexManager.GetChain().Tip())
    {
        tipHash = tip->GetBlockHash();
        return true;
    }

    return false;
}

void CChainCommonent::OnNodeDisconnected(int64_t nodeID, bool /*bInBound*/, int /*disconnectReason*/)
{
    LOCK(cs_xnodeGuard);
    m_nodeCheckPointKnown.erase(nodeID);
}

bool CChainCommonent::NetRequestCheckPoint(ExNode *xnode, int height)
{
    assert(xnode != nullptr);

    std::vector<Checkpoints::CCheckData> vSendData;
    std::vector<Checkpoints::CCheckData> vnHeight;
    Checkpoints::GetCheckpointByHeight(height, vnHeight);
    {
        LOCK(cs_xnodeGuard);
        std::set<int> &checkpointKnown = m_nodeCheckPointKnown[xnode->nodeID];
        for (const auto &point : vnHeight)
        {
            if (checkpointKnown.count(point.getHeight()) == 0)
            {
                checkpointKnown.insert(point.getHeight());
                vSendData.push_back(point);
            }
        }
    }

    if (!vSendData.empty())
    {
        SendNetMessage(xnode->nodeID, NetMsgType::CHECKPOINT, xnode->sendVersion, 0, vSendData);
    }

    return true;
}

bool CChainCommonent::NetReceiveCheckPoint(ExNode *xnode, CDataStream &stream)
{
    assert(xnode != nullptr);

    mlog.debug("enter checkpoint");
    mlog.debug("receive check block list====");

    std::vector<Checkpoints::CCheckData> vdata;
    stream >> vdata;

    Checkpoints::CCheckPointDB cCheckPointDB;
    std::vector<int> toInsertCheckpoints;
    std::vector<Checkpoints::CCheckData> vIndex;
    const CChainParams &chainparams = app().GetChainParams();
    for (const auto &point : vdata)
    {
        if (point.CheckSignature(chainparams.GetCheckPointPKey()))
        {
            if (!cCheckPointDB.ExistCheckpoint(point.getHeight()))
            {
                cCheckPointDB.WriteCheckpoint(point.getHeight(), point);
                /*
                 * add the check point to chainparams
                 */
                chainparams.AddCheckPoint(point.getHeight(), point.getHash());
                toInsertCheckpoints.push_back(point.getHeight());
                vIndex.push_back(point);
            }
        } else
        {
            mlog.debug("check point signature check failed \n");
            break;
        }
        mlog.debug("block height=%d, block hash=%s\n", point.getHeight(), point.getHash().ToString());
    }

    if (!toInsertCheckpoints.empty())
    {
        LOCK(cs_xnodeGuard);
        std::set<int> &checkpointKnown = m_nodeCheckPointKnown[xnode->nodeID];
        checkpointKnown.insert(toInsertCheckpoints.begin(), toInsertCheckpoints.end());
    }

    if (vIndex.size() > 0)
    {
        CValidationState state;
        if (!CheckActiveChain(state, chainparams))
        {
            mlog.error("CheckActiveChain error when receive  checkpoint");
            return false;
        }
    }

    //broadcast the check point if it is necessary
    if (vIndex.size() == 1 && vIndex.size() == vdata.size())
    {
        SendNetMessage(-1, NetMsgType::CHECKPOINT, xnode->sendVersion, 0, vdata);
    }

    return true;
}

bool CChainCommonent::NetRequestBlocks(ExNode *xnode, CDataStream &stream, std::vector<uint256> &blockHashes)
{
    assert(xnode != nullptr);

    CBlockLocator locator;
    uint256 hashStop;
    stream >> locator >> hashStop;

    // We might have announced the currently-being-connected tip using a
    // compact block, which resulted in the peer sending a getblocks
    // request, which we would otherwise respond to without the new block.
    // To avoid this situation we simply verify that we are on our best
    // known chain now. This is super overkill, but we handle it better
    // for getheaders requests, and there are no known nodes which support
    // compact blocks but still use getblocks to request blocks.
    //{
    //    std::shared_ptr<const CBlock> a_recent_block;
    //    {
    //        LOCK(cs_most_recent_block);
    //        a_recent_block = most_recent_block;
    //    }
    //    CValidationState dummy;
    //    ActivateBestChain(dummy, Params(), a_recent_block);
    //}

    LOCK(cs_main);

    // Find the last block the caller has in the main chain
    const CBlockIndex *pindex = cIndexManager.FindForkInGlobalIndex(chainActive, locator);

    // Send the rest of the chain
    if (pindex)
        pindex = cIndexManager.GetChain().Next(pindex);

    int nLimit = 500;
    mlog.debug("getblocks %d to %s limit %d from peer=%d", (pindex ? pindex->nHeight : -1),
               hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, xnode->nodeID);

    const CChainParams &chainparams = app().GetChainParams();
    for (; pindex; pindex = cIndexManager.GetChain().Next(pindex))
    {
        if (pindex->GetBlockHash() == hashStop)
        {
            mlog.debug("  getblocks stopping at %d %s", pindex->nHeight, pindex->GetBlockHash().ToString());
            break;
        }
        // If pruning, don't inv blocks unless we have on disk and are likely to still have
        // for some reasonable time window (1 hour) that block relay might require.
        const int nPrunedBlocksLikelyToHave =
                MIN_BLOCKS_TO_KEEP - 3600 / chainparams.GetConsensus().nPowTargetSpacing;
        if (fPruneMode && (!(pindex->nStatus & BLOCK_HAVE_DATA) ||
                           pindex->nHeight <= Tip()->nHeight - nPrunedBlocksLikelyToHave))
        {
            mlog.debug(" getblocks stopping, pruned or too old block at %d %s", pindex->nHeight,
                       pindex->GetBlockHash().ToString());
            break;
        }
        blockHashes.emplace_back(pindex->GetBlockHash());
        if (--nLimit <= 0)
        {
            // When this block is requested, we'll send an inv that'll
            // trigger the peer to getblocks the next batch of inventory.
            mlog.debug("  getblocks stopping at limit %d %s", pindex->nHeight,
                       pindex->GetBlockHash().ToString());
            break;
        }
    }
    return true;
}

bool CChainCommonent::NetRequestHeaders(ExNode *xnode, CDataStream &stream)
{
    assert(xnode != nullptr);

    if (IsInitialBlockDownload() && !IsFlagsBitOn(xnode->flags, NF_WHITELIST))
    {
        mlog.debug("Ignoring getheaders from peer=%d because node is in initial block download",
                   xnode->nodeID);
        return true;
    }

    CBlockLocator locator;
    uint256 hashStop;
    stream >> locator >> hashStop;

    const CBlockIndex *pindex = nullptr;
    if (locator.IsNull())
    {
        // If locator is null, return the hashStop block
        pindex = cIndexManager.GetBlockIndex(hashStop);
        if (!pindex)
        {
            return true;
        }
    } else
    {
        // Find the last block the caller has in the main chain
        pindex = cIndexManager.FindForkInGlobalIndex(cIndexManager.GetChain(), locator);
        if (pindex)
            pindex = cIndexManager.GetChain().Next(pindex);
    }

    // we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at the end
    std::vector<CBlock> vHeaders;
    int nLimit = MAX_HEADERS_RESULTS;

    mlog.debug("getheaders %d to %s from peer=%d\n", (pindex ? pindex->nHeight : -1),
               hashStop.IsNull() ? "end" : hashStop.ToString(), xnode->nodeID);

    for (; pindex; pindex = cIndexManager.GetChain().Next(pindex))
    {
        vHeaders.push_back(pindex->GetBlockHeader());
        if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
            break;
    }

    return SendNetMessage(xnode->nodeID, NetMsgType::HEADERS, xnode->sendVersion, 0, vHeaders);
}

bool CChainCommonent::NetReceiveHeaders(ExNode *xnode, CDataStream &stream)
{
    assert(xnode != nullptr);

    std::vector<CBlockHeader> headers;

    // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
    unsigned int nCount = ReadCompactSize(stream);
    if (nCount > MAX_HEADERS_RESULTS)
    {
        LOCK(cs_main);
        xnode->nMisbehavior = 20;
        return error("headers message size = %u", nCount);
    }

    headers.resize(nCount);
    for (unsigned int n = 0; n < nCount; n++)
    {
        stream >> headers[n];
        ReadCompactSize(stream); // ignore tx count; assume it is 0.
    }

    return NetReceiveHeaders(xnode, headers);
}

bool CChainCommonent::NetReceiveHeaders(ExNode *xnode, const std::vector<CBlockHeader> &headers)
{
    assert(xnode != nullptr);

    size_t nCount = headers.size();
    if (nCount == 0)
        return true;

    // If this looks like it could be a block announcement (nCount <
    // MAX_BLOCKS_TO_ANNOUNCE), use special logic for handling headers that
    // don't connect:
    // - Send a getheaders message in response to try to connect the chain.
    // - The peer can send up to MAX_UNCONNECTING_HEADERS in a row that
    //   don't connect before giving DoS points
    // - Once a headers message is received that is valid and does connect,
    //   nUnconnectingHeaders gets reset back to 0.
    if (!DoesBlockExist(headers[0].hashPrevBlock) && nCount < MAX_BLOCKS_TO_ANNOUNCE)
    {
        xnode->nUnconnectingHeaders++;
        SendNetMessage(xnode->nodeID, NetMsgType::GETHEADERS, xnode->sendVersion, 0,
                       cIndexManager.GetChain().GetLocator(pindexBestHeader), uint256());

        mlog.debug(
                "received header %s: missing prev block %s, sending getheaders (%d) to end (peer=%d, nUnconnectingHeaders=%d)",
                headers[0].GetHash().ToString(),
                headers[0].hashPrevBlock.ToString(),
                pindexBestHeader->nHeight,
                xnode->nodeID, xnode->nUnconnectingHeaders);

        // Set hashLastUnknownBlock for this peer, so that if we
        // eventually get the headers - even from a different peer -
        // we can use this peer to download.
        ///UpdateBlockAvailability(pfrom->GetId(), headers.back().GetHash());

        if (xnode->nUnconnectingHeaders % MAX_UNCONNECTING_HEADERS == 0)
        {
            xnode->nMisbehavior += 20;
        }
        return true;
    }

    uint256 hashLastBlock;
    for (const CBlockHeader &header : headers)
    {
        if (!hashLastBlock.IsNull() && header.hashPrevBlock != hashLastBlock)
        {
            xnode->nMisbehavior += 20;
            return error("non-continuous headers sequence");
        }
        hashLastBlock = header.GetHash();
    }

    // If we don't have the last header, then they'll have given us
    // something new (if these headers are valid).
    bool received_new_header = !DoesBlockExist(hashLastBlock);

    CValidationState state;
    CBlockHeader first_invalid_header;
    const CBlockIndex *pindexLast = nullptr;
    if (!ProcessNewBlockHeaders(headers, state, app().GetChainParams(), &pindexLast, &first_invalid_header))
    {
        int nDoS;
        if (state.IsInvalid(nDoS))
        {
            if (nDoS > 0)
            {
                xnode->nMisbehavior += nDoS;
            }
            if (IsFlagsBitOn(xnode->flags, NF_OUTBOUND) &&
                !IsFlagsBitOn(xnode->flags, NF_MANUALCONN) &&
                DoesBlockExist(first_invalid_header.GetHash()))
            {
                // Goal: don't allow outbound peers to use up our outbound
                // connection slots if they are on incompatible chains.
                //
                // We ask the caller to set punish_invalid appropriately based
                // on the peer and the method of header delivery (compact
                // blocks are allowed to be invalid in some circumstances,
                // under BIP 152).
                // Here, we try to detect the narrow situation that we have a
                // valid block header (ie it was valid at the time the header
                // was received, and hence stored in mapBlockIndex) but know the
                // block is invalid, and that a peer has announced that same
                // block as being on its active chain.
                // Disconnect the peer in such a situation.
                //
                // Note: if the header that is invalid was not accepted to our
                // mapBlockIndex at all, that may also be grounds for
                // disconnecting the peer, as the chain they are on is likely
                // to be incompatible. However, there is a circumstance where
                // that does not hold: if the header's timestamp is more than
                // 2 hours ahead of our current time. In that case, the header
                // may become valid in the future, and we don't want to
                // disconnect a peer merely for serving us one too-far-ahead
                // block header, to prevent an attacker from splitting the
                // network by mining a block right at the 2 hour boundary.
                //
                // TODO: update the DoS logic (or, rather, rewrite the
                // DoS-interface between validation and net_processing) so that
                // the interface is cleaner, and so that we disconnect on all the
                // reasons that a peer's headers chain is incompatible
                // with ours (eg block->nVersion softforks, MTP violations,
                // etc), and not just the duplicate-invalid case.
                SetFlagsBit(xnode->retFlags, NF_DISCONNECT);
            }
            return error("invalid header received");
        }
    }

    {
        if (xnode->nUnconnectingHeaders > 0)
        {
            mlog.debug("peer=%d: resetting nUnconnectingHeaders (%d -> 0)\n", xnode->nodeID,
                       xnode->nUnconnectingHeaders);
        }
        xnode->nUnconnectingHeaders = 0;

        //assert(pindexLast);
        //UpdateBlockAvailability(pfrom->GetId(), pindexLast->GetBlockHash());

        // From here, pindexBestKnownBlock should be guaranteed to be non-null,
        // because it is set in UpdateBlockAvailability. Some nullptr checks
        // are still present, however, as belt-and-suspenders.

        //if (received_new_header && pindexLast->nChainWork > chainActive.Tip()->nChainWork)
        //{
        //    nodestate->m_last_block_announcement = GetTime();
        //}

        if (nCount == MAX_HEADERS_RESULTS)
        {
            // Headers message had its maximum size; the peer may have more headers.
            // TODO: optimize: if pindexLast is an ancestor of chainActive.Tip or pindexBestHeader, continue
            // from there instead.
            mlog.debug("more getheaders (%d) to end to peer=%d (startheight:%d)\n", pindexLast->nHeight,
                       xnode->nodeID, xnode->startHeight);

            SendNetMessage(xnode->nodeID, NetMsgType::GETHEADERS, xnode->sendVersion, 0,
                           cIndexManager.GetChain().GetLocator(pindexLast), uint256());
        }

        bool fCanDirectFetch = Tip()->GetBlockTime() > (GetAdjustedTime() -
                                                        app().GetChainParams().GetConsensus().nPowTargetSpacing *
                                                        20);

        // If this set of headers is valid and ends in a block with at least as
        // much work as our tip, download as much as possible.
        if (fCanDirectFetch && pindexLast->IsValid(BLOCK_VALID_TREE) &&
            Tip()->nChainWork <= pindexLast->nChainWork)
        {
            std::vector<const CBlockIndex *> vToFetch;
            const CBlockIndex *pindexWalk = pindexLast;
            // Calculate all the blocks we'd need to switch to pindexLast, up to a limit.
            while (pindexWalk && !cIndexManager.GetChain().Contains(pindexWalk) &&
                   vToFetch.size() <= MAX_BLOCKS_IN_TRANSIT_PER_PEER)
            {
                if (!(pindexWalk->nStatus & BLOCK_HAVE_DATA) &&
                    //!mapBlocksInFlight.count(pindexWalk->GetBlockHash()) &&
                    (!IsWitnessEnabled(pindexWalk->pprev, app().GetChainParams().GetConsensus()) ||
                     IsFlagsBitOn(xnode->flags, NF_WITNESS)))
                {
                    // We don't have this block, and it's not yet in flight.
                    vToFetch.push_back(pindexWalk);
                }
                pindexWalk = pindexWalk->pprev;
            }
            // If pindexWalk still isn't on our main chain, we're looking at a
            // very large reorg at a time we think we're close to caught up to
            // the main chain -- this shouldn't really happen.  Bail out on the
            // direct fetch and rely on parallel download instead.
            if (!cIndexManager.GetChain().Contains(pindexWalk))
            {
                mlog.debug("Large reorg, won't direct fetch to %s (%d)",
                           pindexLast->GetBlockHash().ToString(),
                           pindexLast->nHeight);
            } else
            {
                uint32_t nFetchFlags = 0;
                if (IsFlagsBitOn(xnode->nLocalServices, NODE_WITNESS) &&
                    IsFlagsBitOn(xnode->flags, NF_WITNESS))
                    nFetchFlags = MSG_WITNESS_FLAG;

                std::vector<CInv> vGetData;
                // Download as much as possible, from earliest to latest.
                for (const CBlockIndex *pindex : reverse_iterate(vToFetch))
                {
                    if (xnode->nBlocksInFlight >= MAX_BLOCKS_IN_TRANSIT_PER_PEER)
                    {
                        // Can't download any more from this peer
                        break;
                    }

                    vGetData.push_back(CInv(MSG_BLOCK | nFetchFlags, pindex->GetBlockHash()));
                    // MarkBlockAsInFlight(pfrom->GetId(), pindex->GetBlockHash(), pindex);
                    mlog.debug("Requesting block %s from  peer=%d",
                               pindex->GetBlockHash().ToString(), xnode->nodeID);
                }
                if (vGetData.size() > 1)
                {
                    mlog.debug("Downloading blocks toward %s (%d) via headers direct fetch",
                               pindexLast->GetBlockHash().ToString(), pindexLast->nHeight);
                }
                if (vGetData.size() > 0)
                {
                    if (IsFlagsBitOn(xnode->flags, NF_DESIREDCMPCTVERSION) && vGetData.size() == 1 &&
                        //mapBlocksInFlight.size() == 1 &&
                        pindexLast->pprev->IsValid(BLOCK_VALID_CHAIN))
                    {
                        // In any case, we want to download using a compact block, not a regular one
                        vGetData[0] = CInv(MSG_CMPCT_BLOCK, vGetData[0].hash);
                    }
                    SendNetMessage(xnode->nodeID, NetMsgType::GETDATA, xnode->sendVersion, 0, vGetData);
                }
            }
        }
        // If we're in IBD, we want outbound peers that will serve us a useful
        // chain. Disconnect peers that are on chains with insufficient work.
        //        if (IsInitialBlockDownload() && nCount != MAX_HEADERS_RESULTS)
        //        {
        //            // When nCount < MAX_HEADERS_RESULTS, we know we have no more
        //            // headers to fetch from this peer.
        //            if (nodestate->pindexBestKnownBlock && nodestate->pindexBestKnownBlock->nChainWork < nMinimumChainWork)
        //            {
        //                // This peer has too little work on their headers chain to help
        //                // us sync -- disconnect if using an outbound slot (unless
        //                // whitelisted or addnode).
        //                // Note: We compare their tip to nMinimumChainWork (rather than
        //                // chainActive.Tip()) because we won't start block download
        //                // until we have a headers chain that has at least
        //                // nMinimumChainWork, even if a peer has a chain past our tip,
        //                // as an anti-DoS measure.
        //                if (IsOutboundDisconnectionCandidate(pfrom))
        //                {
        //                    LogPrintf("Disconnecting outbound peer %d -- headers chain has insufficient work\n",
        //                              xnode->nodeID);
        //                    SetFlagsBit(xnode->retFlags, NF_DISCONNECT);
        //                }
        //            }
        //        }
        //
        //        if (!pfrom->fDisconnect && IsOutboundDisconnectionCandidate(pfrom) &&
        //            nodestate->pindexBestKnownBlock != nullptr)
        //        {
        //            // If this is an outbound peer, check to see if we should protect
        //            // it from the bad/lagging chain logic.
        //            if (g_outbound_peers_with_protect_from_disconnect < MAX_OUTBOUND_PEERS_TO_PROTECT_FROM_DISCONNECT &&
        //                nodestate->pindexBestKnownBlock->nChainWork >= chainActive.Tip()->nChainWork &&
        //                !nodestate->m_chain_sync.m_protect)
        //            {
        //                LogPrint(BCLog::NET, "Protecting outbound peer=%d from eviction\n", pfrom->GetId());
        //                nodestate->m_chain_sync.m_protect = true;
        //                ++g_outbound_peers_with_protect_from_disconnect;
        //            }
        //        }
    }

    return true;
}

bool CChainCommonent::NetRequestBlockData(ExNode *xnode, uint256 blockHash, int blockType)
{
    assert(xnode != nullptr);

    GET_NET_INTERFACE(ifNetObj);
    assert(ifNetObj != nullptr);

    bool isOK = false;
    const Consensus::Params &consensusParams = app().GetChainParams().GetConsensus();

    //    std::shared_ptr<const CBlock> a_recent_block;
    //    std::shared_ptr<const CBlockHeaderAndShortTxIDs> a_recent_compact_block;
    //    bool fWitnessesPresentInARecentCompactBlock;
    //    {
    //        LOCK(cs_most_recent_block);
    //        a_recent_block = most_recent_block;
    //        a_recent_compact_block = most_recent_compact_block;
    //        fWitnessesPresentInARecentCompactBlock = fWitnessesPresentInMostRecentCompactBlock;
    //    }

    CBlockIndex *bi = cIndexManager.GetBlockIndex(blockHash);
    if (bi != nullptr)
    {
        if (bi->nChainTx && !bi->IsValid(BLOCK_VALID_SCRIPTS) && bi->IsValid(BLOCK_VALID_TREE))
        {
            // If we have the block and all of its parents, but have not yet validated it,
            // we might be in the middle of connecting it (ie in the unlock of cs_main
            // before ActivateBestChain but after AcceptBlock).
            // In this case, we need to run ActivateBestChain prior to checking the relay
            // conditions below.
            // CValidationState dummy;
            // ActivateBestChain(dummy, Params(), a_recent_block);
        }

        if (cIndexManager.GetChain().Contains(bi))
        {
            isOK = true;
        } else
        {
            static const int nOneMonth = 30 * 24 * 60 * 60;
            // To prevent fingerprinting attacks, only send blocks outside of the active
            // chain if they are valid, and no more than a month older (both in time, and in
            // best equivalent proof of work) than the best header chain we know about.
            isOK = bi->IsValid(BLOCK_VALID_SCRIPTS) && (pindexBestHeader != nullptr) &&
                   (pindexBestHeader->GetBlockTime() - bi->GetBlockTime() < nOneMonth) &&
                   (GetBlockProofEquivalentTime(*pindexBestHeader, *bi, *pindexBestHeader,
                                                consensusParams) < nOneMonth);
            if (!isOK)
            {
                mlog.debug("%s: ignoring request from peer=%i for old block that isn't in the main chain",
                           __func__, xnode->nodeID);
            }
        }
    }
    // disconnect node in case we have reached the outbound limit for serving historical blocks
    // never disconnect whitelisted nodes
    static const int nOneWeek = 7 * 24 * 60 * 60; // assume > 1 week = historical
    if (isOK && ifNetObj->OutboundTargetReached(true) && (((pindexBestHeader != nullptr) &&
                                                           (pindexBestHeader->GetBlockTime() -
                                                            bi->GetBlockTime() > nOneWeek)) ||
                                                          blockType == MSG_FILTERED_BLOCK) &&
        !IsFlagsBitOn(xnode->flags, NF_WHITELIST))
    {
        mlog.debug("historical block serving limit reached, disconnect peer=%d\n",
                   xnode->nodeID);

        //disconnect node
        SetFlagsBit(xnode->retFlags, NF_DISCONNECT);
        isOK = false;
    }
    // Pruned nodes may have deleted the block, so check whether
    // it's available before trying to send.
    if (isOK && (bi->nStatus & BLOCK_HAVE_DATA))
    {
        std::shared_ptr<const CBlock> pblock;
        //        if (a_recent_block && a_recent_block->GetHash() == (*mi).second->GetBlockHash())
        //        {
        //            pblock = a_recent_block;
        //        } else
        {
            // Send block from disk
            std::shared_ptr<CBlock> pblockRead = std::make_shared<CBlock>();
            if (!ReadBlockFromDisk(*pblockRead, bi, consensusParams))
                assert(!"cannot load block from disk");
            pblock = pblockRead;
        }
        if (blockType == MSG_BLOCK)
        {
            SendNetMessage(xnode->nodeID, NetMsgType::BLOCK, xnode->sendVersion, SERIALIZE_TRANSACTION_NO_WITNESS,
                           *pblock);
        } else if (blockType == MSG_WITNESS_BLOCK)
        {
            SendNetMessage(xnode->nodeID, NetMsgType::BLOCK, xnode->sendVersion, 0, *pblock);
        } else if (blockType == MSG_FILTERED_BLOCK)
        {
            bool sendMerkleBlock = false;
            CMerkleBlock merkleBlock;
            //            {
            //                LOCK(pfrom->cs_filter);
            //                if (pfrom->pfilter)
            //                {
            //                    sendMerkleBlock = true;
            //                    merkleBlock = CMerkleBlock(*pblock, *pfrom->pfilter);
            //                }
            //            }
            if (sendMerkleBlock)
            {
                SendNetMessage(xnode->nodeID, NetMsgType::MERKLEBLOCK, xnode->sendVersion, 0, merkleBlock);
                // CMerkleBlock just contains hashes, so also push any transactions in the block the client did not see
                // This avoids hurting performance by pointlessly requiring a round-trip
                // Note that there is currently no way for a node to request any single transactions we didn't send here -
                // they must either disconnect and retry or request the full block.
                // Thus, the protocol spec specified allows for us to provide duplicate txn here,
                // however we MUST always provide at least what the remote peer needs
                typedef std::pair<unsigned int, uint256> PairType;
                for (PairType &pair : merkleBlock.vMatchedTxn)
                    SendNetMessage(xnode->nodeID, NetMsgType::TX, xnode->sendVersion, SERIALIZE_TRANSACTION_NO_WITNESS,
                                   *pblock->vtx[pair.first]);
            }
            // else
            // no response
        } else if (blockType == MSG_CMPCT_BLOCK)
        {
            //            // If a peer is asking for old blocks, we're almost guaranteed
            //            // they won't have a useful mempool to match against a compact block,
            //            // and we don't feel like constructing the object for them, so
            //            // instead we respond with the full, non-compact block.
            //            bool fPeerWantsWitness = IsFlagsBitOn(xnode->flags, NF_WANTCMPCTWITNESS);
            //            int nSendFlags = fPeerWantsWitness ? 0 : SERIALIZE_TRANSACTION_NO_WITNESS;
            //            if (CanDirectFetch(consensusParams) &&
            //                bi->nHeight >= cIndexManager.GetChain().Height() - MAX_CMPCTBLOCK_DEPTH)
            //            {
            //                if ((fPeerWantsWitness || !fWitnessesPresentInARecentCompactBlock) &&
            //                    a_recent_compact_block &&
            //                    a_recent_compact_block->header.GetHash() == bi->GetBlockHash())
            //                {
            //                    SendNetMessage(xnode->nodeID, NetMsgType::MERKLEBLOCK, xnode->sendVersion, nSendFlags, *a_recent_compact_block);
            //                }
            //                else
            //                {
            //                    CBlockHeaderAndShortTxIDs cmpctblock(*pblock, fPeerWantsWitness);
            //                    SendNetMessage(xnode->nodeID, NetMsgType::CMPCTBLOCK, xnode->sendVersion, nSendFlags, cmpctblock);
            //                }
            //            }
            //            else
            //            {
            //                SendNetMessage(xnode->nodeID, NetMsgType::BLOCK, xnode->sendVersion, nSendFlags, *pblock);
            //            }
        }
    }
    return isOK;
}

bool CChainCommonent::NetReceiveBlockData(ExNode *xnode, CDataStream &stream, uint256 &blockHash)
{
    assert(xnode != nullptr);

    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    stream >> *
            pblock;

    mlog.debug("received block %s peer=%d", pblock->GetHash().ToString(), xnode->nodeID);

    bool forceProcessing = false;
    const uint256 hash(pblock->GetHash());
    //    {
    //        LOCK(cs_main);
    //        // Also always process if we requested the block explicitly, as we may
    //        // need it even though it is not a candidate for a new best tip.
    //        forceProcessing |= MarkBlockAsReceived(hash);
    //        // mapBlockSource is only used for sending reject messages and DoS scores,
    //        // so the race between here and cs_main in ProcessNewBlock is fine.
    //        mapBlockSource.emplace(hash, std::make_pair(pfrom->GetId(), true));
    //    }

    bool fNewBlock = false;
    ProcessNewBlock(app().GetChainParams(), pblock, forceProcessing, &fNewBlock);
    if (fNewBlock)
    {
        SetFlagsBit(xnode->retFlags, NF_NEWBLOCK);
    }

    return true;
}

bool CChainCommonent::NetRequestBlockTxn(ExNode *xnode, CDataStream &stream)
{
    assert(xnode != nullptr);

    BlockTransactionsRequest req;
    stream >> req;

    //    std::shared_ptr<const CBlock> recent_block;
    //    {
    //        LOCK(cs_most_recent_block);
    //        if (most_recent_block_hash == req.blockhash)
    //            recent_block = most_recent_block;
    //        // Unlock cs_most_recent_block to avoid cs_main lock inversion
    //    }
    //
    //    if (recent_block)
    //    {
    //        NetSendBlockTransactions(xnode, req, *recent_block);
    //        return true;
    //    }

    CBlockIndex *bi = cIndexManager.GetBlockIndex(req.blockhash);
    if (!bi || !(bi->nStatus & BLOCK_HAVE_DATA))
    {
        mlog.debug("Peer %d sent us a getblocktxn for a block we don't have", xnode->nodeID);
        return true;
    }

    if (bi->nHeight < chainActive.Height() - MAX_BLOCKTXN_DEPTH)
    {
        // If an older block is requested (should never happen in practice,
        // but can happen in tests) send a block response instead of a
        // blocktxn response. Sending a full block response instead of a
        // small blocktxn response is preferable in the case where a peer
        // might maliciously send lots of getblocktxn requests to trigger
        // expensive disk reads, because it will require the peer to
        // actually receive all the data read from disk over the network.
        mlog.debug("Peer %d sent us a getblocktxn for a block > %i deep", xnode->nodeID, MAX_BLOCKTXN_DEPTH);
        int blockType = IsFlagsBitOn(xnode->flags, NF_WANTCMPCTWITNESS) ? MSG_WITNESS_BLOCK : MSG_BLOCK;
        NetRequestBlockData(xnode, req.blockhash, blockType);
        return true;
    }

    CBlock block;
    bool ret = ReadBlockFromDisk(block, bi, app().GetChainParams().GetConsensus());
    assert(ret);

    return NetSendBlockTransactions(xnode, req, block);
}

bool
CChainCommonent::ProcessNewBlock(const std::shared_ptr<const CBlock> pblock, bool fForceProcessing, bool *fNewBlock)
{
    return ProcessNewBlock(app().GetChainParams(), pblock, fForceProcessing, fNewBlock);

    //    const CChainParams &chainparams = app().GetChainParams();
    //    {
    //        CBlockIndex *pindex = nullptr;
    //        if (fNewBlock)
    //            *fNewBlock = false;
    //        CValidationState state;
    //        // Ensure that CheckBlock() passes before calling AcceptBlock, as
    //        // belt-and-suspenders.
    //        bool ret = CheckBlock(*pblock, state, chainparams.GetConsensus());
    //
    //        LOCK(cs_main);
    //
    //        if (ret)
    //        {
    //            // Store to disk
    //            ret = AcceptBlock(pblock, state, chainparams, &pindex, fForceProcessing, nullptr, fNewBlock);
    //        }
    //        CheckBlockIndex(chainparams.GetConsensus());
    //        if (!ret)
    //        {
    //            GetMainSignals().BlockChecked(*pblock, state);
    //            return error("%s: AcceptBlock FAILED", __func__);
    //        }
    //    }
    //
    //    NotifyHeaderTip();
    //
    //    CValidationState state; // Only used to report errors, not invalidity - ignore it
    //    if (!ActivateBestChain(state, chainparams, pblock))
    //        return error("%s: ActivateBestChain failed", __func__);
    //
    //    return true;
}

bool CChainCommonent::NetSendBlockTransactions(ExNode *xnode, const BlockTransactionsRequest &req, const CBlock &block)
{
    BlockTransactions resp(req);
    for (size_t i = 0; i < req.indexes.size(); i++)
    {
        if (req.indexes[i] >= block.vtx.size())
        {
            xnode->nMisbehavior = 100;
            mlog.debug("Peer %d sent us a getblocktxn with out-of-bounds tx indices", xnode->nodeID);
            return false;
        }
        resp.txn[i] = block.vtx[req.indexes[i]];
    }

    int nSendFlags = IsFlagsBitOn(xnode->flags, NF_WANTCMPCTWITNESS) ? 0 : SERIALIZE_TRANSACTION_NO_WITNESS;
    return SendNetMessage(xnode->nodeID, NetMsgType::BLOCKTXN, xnode->sendVersion, nSendFlags, resp);
}

CBlockIndex *CChainCommonent::Tip()
{
    return cIndexManager.GetChain().Tip();
}

void CChainCommonent::SetTip(CBlockIndex *pIndexTip)
{
    cIndexManager.GetChain().SetTip(pIndexTip);
}

bool CChainCommonent::ReplayBlocks()
{
    const CChainParams &params = app().GetChainParams();

    CCoinsView *view(cViewManager.GetCoinViewDB());
    CCoinsViewCache cache(view);

    std::vector<uint256> vecHashHeads = view->GetHeadBlocks();
    if (vecHashHeads.empty())
        return true; // We're already in a consistent state.
    if (vecHashHeads.size() != 2)
        return error("ReplayBlocks(): unknown inconsistent state");

    mlog.info("Replaying blocks\n");

    const CBlockIndex *pIndexOld = nullptr;  // Old tip during the interrupted flush.
    const CBlockIndex *pIndexNew;            // New tip during the interrupted flush.
    const CBlockIndex *pIndexFork = nullptr; // Latest block common to both the old and the new tip.

    pIndexNew = cIndexManager.GetBlockIndex(vecHashHeads[0]);
    if (!pIndexNew)
    {
        return false;
    }

    if (!vecHashHeads[1].IsNull())
    {
        pIndexOld = cIndexManager.GetBlockIndex(vecHashHeads[1]);
        if (!pIndexOld)
        {
            return false;
        }
        pIndexFork = cIndexManager.LastCommonAncestor(pIndexNew, pIndexOld);
        assert(pIndexFork != nullptr);
    }

    // Rollback along the old branch.
    while (pIndexOld != pIndexFork)
    {
        if (pIndexOld->nHeight > 0) // Never disconnect the genesis block.
        {
            CBlock block;
            if (!ReadBlockFromDisk(block, pIndexOld, params.GetConsensus()))
            {
                return error("RollbackBlock(): ReadBlockFromDisk() failed at %d, hash=%s", pIndexOld->nHeight,
                             pIndexOld->GetBlockHash().ToString());
            }

            mlog.info("Rolling back %s (%i)\n", pIndexOld->GetBlockHash().ToString(), pIndexOld->nHeight);

            DisconnectResult res = cViewManager.DisconnectBlock(block, pIndexOld, cache);
            if (res == DISCONNECT_FAILED)
            {
                return error("RollbackBlock(): DisconnectBlock failed at %d, hash=%s", pIndexOld->nHeight,
                             pIndexOld->GetBlockHash().ToString());
            }
            // If DISCONNECT_UNCLEAN is returned, it means a non-existing UTXO was deleted, or an existing UTXO was
            // overwritten. It corresponds to cases where the block-to-be-disconnect never had all its operations
            // applied to the UTXO set. However, as both writing a UTXO and deleting a UTXO are idempotent operations,
            // the result is still a version of the UTXO set with the effects of that block undone.
        }
        pIndexOld = pIndexOld->pprev;
    }

    // Roll forward from the forking point to the new tip.
    int iForkHeight = pIndexFork ? pIndexFork->nHeight : 0;
    for (int nHeight = iForkHeight + 1; nHeight <= pIndexNew->nHeight; ++nHeight)
    {
        const CBlockIndex *pIndex = pIndexNew->GetAncestor(nHeight);
        mlog.info("Rolling forward %s (%i)\n", pIndex->GetBlockHash().ToString(), nHeight);
        CBlock block;
        if (!ReadBlockFromDisk(block, pIndex, params.GetConsensus()))
        {
            return error("ReplayBlock(): ReadBlockFromDisk failed at %d, hash=%s", pIndex->nHeight,
                         pIndex->GetBlockHash().ToString());
        }
        if (!cViewManager.ConnectBlock(block, pIndex, cache))
            return false;
    }

    cache.SetBestBlock(pIndexNew->GetBlockHash());
    cache.Flush();

    return true;
}

bool CChainCommonent::NeedFullFlush(FlushStateMode mode)
{
    return true;
}

bool CChainCommonent::FlushStateToDisk(CValidationState &state, FlushStateMode mode, const CChainParams &chainparams)
{
    int iLastBlockFile = cIndexManager.GetLastBlockFile();
    int iSize = cIndexManager.GetBlockFileInfo()[iLastBlockFile].nSize;
    int iUndoSize = cIndexManager.GetBlockFileInfo()[iLastBlockFile].nUndoSize;

    // block file flush
    FlushBlockFile(iLastBlockFile, iSize, iUndoSize);

    // block index flush
    if (!cIndexManager.Flush())
    {
        assert(0);
        return false;
    }

    // view flush
    if (!cViewManager.Flush())
    {
        return false;
    }

    return true;
}

void CChainCommonent::FlushStateToDisk()
{
    CValidationState state;
    const CChainParams &params = app().GetChainParams();
    FlushStateToDisk(state, FLUSH_STATE_ALWAYS, params);
}

bool CChainCommonent::IsInitialBlockDownload()
{
    // Once this function has returned false, it must remain false.
    static std::atomic<bool> latchToFalse{false};
    // Optimization: pre-test latch before taking the lock.
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;

    LOCK(cs);
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;
    if (fImporting || fReindex)
        return true;
    if (Tip() == nullptr)
        return true;
    if (Tip()->nChainWork < nMinimumChainWork)
        return true;
    if (Tip()->GetBlockTime() < (GetTime() - nMaxTipAge))
        return true;
    mlog.info("Leaving InitialBlockDownload (latching to false)");
    latchToFalse.store(true, std::memory_order_relaxed);
    return false;
}

void CChainCommonent::DoWarning(const std::string &strWarning)
{
    static bool fWarned = false;
    SetMiscWarning(strWarning);
    if (!fWarned)
    {
        AlertNotify(strWarning);
        fWarned = true;
    }
}

/** Update chainActive and related internal data structures. */
void CChainCommonent::UpdateTip(CBlockIndex *pindexNew, const CChainParams &chainParams)
{
    CChain chainActive = cIndexManager.GetChain();

    chainActive.SetTip(pindexNew);

    //    ITxMempoolComponent *txmempool = (CTxMemPool *)appbase::CApp::Instance().FindComponent<ITxMempoolComponent>();
    // New best block
    //    txmempool->AddTransactionsUpdated(1); todo

    cvBlockChange.notify_all();

    std::vector<std::string> warningMessages;
    if (!IsInitialBlockDownload())
    {
        int nUpgraded = 0;
        const CBlockIndex *pindex = chainActive.Tip();
        for (int bit = 0; bit < VERSIONBITS_NUM_BITS; bit++)
        {
            WarningBitsConditionChecker checker(bit);
            ThresholdState state = checker.GetStateFor(pindex, chainParams.GetConsensus(), warningcache[bit]);
            if (state == THRESHOLD_ACTIVE || state == THRESHOLD_LOCKED_IN)
            {
                const std::string strWarning = strprintf(_("Warning: unknown new rules activated (versionbit %i)"),
                                                         bit);
                if (state == THRESHOLD_ACTIVE)
                {
                    DoWarning(strWarning);
                } else
                {
                    warningMessages.push_back(strWarning);
                }
            }
        }
        // Check the version of the last 100 blocks to see if we need to upgrade:
        for (int i = 0; i < 100 && pindex != nullptr; i++)
        {
            int32_t nExpectedVersion = ComputeBlockVersion(pindex->pprev, chainParams.GetConsensus());
            if (pindex->nVersion > VERSIONBITS_LAST_OLD_BLOCK_VERSION && (pindex->nVersion & ~nExpectedVersion) != 0)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            warningMessages.push_back(strprintf(_("%d of last 100 blocks have unexpected version"), nUpgraded));
        if (nUpgraded > 100 / 2)
        {
            std::string strWarning = _(
                    "Warning: Unknown block versions being mined! It's possible unknown rules are in effect");
            //             notify GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            DoWarning(strWarning);
        }
    }
    mlog.debug(
            "new best=%s height=%d version=0x%08x log2_work=%.8g tx=%lu date='%s' progress=%f cache=%.1fMiB(%utxo)", \
            chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(), chainActive.Tip()->nVersion, \
            log(chainActive.Tip()->nChainWork.getdouble()) / log(2.0), (unsigned long)chainActive.Tip()->nChainTx, \
            DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()), \
            GuessVerificationProgress(chainParams.TxData(), chainActive.Tip()), \
            cViewManager.GetCoinsTip()->DynamicMemoryUsage() * (1.0 / (1 << 20)), \
            cViewManager.GetCoinsTip()->GetCacheSize());
    if (!warningMessages.empty())
        mlog.debug(" warning='%s'", boost::algorithm::join(warningMessages, ", "));

}

bool CChainCommonent::DisconnectTip(CValidationState &state, const CChainParams &chainparams,
                                    DisconnectedBlockTransactions *disconnectpool)
{
    CBlockIndex *pIndexDelete = Tip();
    assert(pIndexDelete);

    // Read block from disk.
    std::shared_ptr<CBlock> pBlock = std::make_shared<CBlock>();
    CBlock &block = *pBlock;
    const CChainParams &params = app().GetChainParams();

    if (!ReadBlockFromDisk(block, pIndexDelete, params.GetConsensus()))
    {
        return AbortNode(state, "Failed to read block");
    }
    // Apply the block atomically to the chain state.
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(cViewManager.GetCoinsTip());
        assert(view.GetBestBlock() == pIndexDelete->GetBlockHash());
        if (cViewManager.DisconnectBlock(block, pIndexDelete, view))
        {
            return error("DisconnectTip(): DisconnectBlock %s failed", pIndexDelete->GetBlockHash().ToString());
        }

        bool bFlush = view.Flush();
        assert(bFlush);
    }

    mlog.info("- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);

    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED, chainparams))
    {
        return false;
    }

    if (disconnectpool)
    {
        // Save transactions to re-add to mempool at end of reorg
        for (auto it = block.vtx.rbegin(); it != block.vtx.rend(); ++it)
        {
            disconnectpool->addTransaction(*it);
        }

        ITxMempoolComponent *txmempool = (CTxMemPool *)appbase::CApp::Instance().FindComponent<ITxMempoolComponent>();

        while (disconnectpool->DynamicMemoryUsage() > MAX_DISCONNECTED_TX_POOL_SIZE * 1000)
        {
            // Drop the earliest entry, and remove its children from the mempool.
            auto it = disconnectpool->queuedTx.get<insertion_order>().begin();
            //            txmempool->removeRecursive(**it, MemPoolRemovalReason::REORG);  todu
            disconnectpool->removeEntry(it);
        }
    }

    // Update chainActive and related variables.
    UpdateTip(pIndexDelete->pprev, chainparams);

    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    GetMainSignals().BlockDisconnected(pBlock);
    return true;
}

static bool
CheckBlockHeader(const CBlockHeader &block, CValidationState &state, const Consensus::Params &consensusParams,
                 bool fCheckPOW = true)
{
    // Check proof of work matches claimed amount
    if (fCheckPOW && !CheckProofOfWork(block.GetHash(), block.nBits, consensusParams))
        return state.DoS(50, false, REJECT_INVALID, "high-hash", false, "proof of work failed");

    return true;
}

bool CChainCommonent::CheckBlock(const CBlock &block, CValidationState &state, const Consensus::Params &consensusParams,
                                 bool fCheckPOW,
                                 bool fCheckMerkleRoot)
{
    // These are checks that are independent of context.

    if (block.fChecked)
        return true;

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, consensusParams, fCheckPOW))
        return false;

    // Check the merkle root.
    if (fCheckMerkleRoot)
    {
        bool mutated;
        uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS(100, false, REJECT_INVALID, "bad-txnmrklroot", true, "hashMerkleRoot mismatch");

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (mutated)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-duplicate", true, "duplicate transaction");
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.
    // Note that witness malleability is checked in ContextualCheckBlock, so no
    // checks that use witness data may be performed here.

    // Size limits
    if (block.vtx.empty() || block.vtx.size() * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT ||
        ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) *
        WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-length", false, "size limits failed");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-missing", false, "first tx is not coinbase");
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i]->IsCoinBase())
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-multiple", false, "more than one coinbase");

    // Check transactions
    for (const auto &tx : block.vtx)
    {
        if (!tx->CheckTransaction(state, false))
        {
            return state.Invalid(false, state.GetRejectCode(), state.GetRejectReason(),
                                 strprintf("Transaction check failed (tx hash %s) %s", tx->GetHash().ToString(),
                                           state.GetDebugMessage()));
        }

    }

    unsigned int nSigOps = 0;
    for (const auto &tx : block.vtx)
    {
        nSigOps += tx->GetLegacySigOpCount();
    }
    if (nSigOps * WITNESS_SCALE_FACTOR > MAX_BLOCK_SIGOPS_COST)
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-sigops", false, "out-of-bounds SigOpCount");

    if (fCheckPOW && fCheckMerkleRoot)
        block.fChecked = true;

    return true;
}

// Compute at which vout of the block's coinbase transaction the witness
// commitment occurs, or -1 if not found.
static int GetWitnessCommitmentIndex(const CBlock &block)
{
    int commitpos = -1;
    if (!block.vtx.empty())
    {
        for (size_t o = 0; o < block.vtx[0]->vout.size(); o++)
        {
            if (block.vtx[0]->vout[o].scriptPubKey.size() >= 38 && block.vtx[0]->vout[o].scriptPubKey[0] == OP_RETURN &&
                block.vtx[0]->vout[o].scriptPubKey[1] == 0x24 && block.vtx[0]->vout[o].scriptPubKey[2] == 0xaa &&
                block.vtx[0]->vout[o].scriptPubKey[3] == 0x21 && block.vtx[0]->vout[o].scriptPubKey[4] == 0xa9 &&
                block.vtx[0]->vout[o].scriptPubKey[5] == 0xed)
            {
                commitpos = o;
            }
        }
    }
    return commitpos;
}

static unsigned int GetBlockScriptFlags(const CBlockIndex *pindex, const Consensus::Params &consensusparams)
{
    AssertLockHeld(cs_main);

    // BIP16 didn't become active until Apr 1 2012
    int64_t nBIP16SwitchTime = 1333238400;
    bool fStrictPayToScriptHash = (pindex->GetBlockTime() >= nBIP16SwitchTime);

    unsigned int flags = fStrictPayToScriptHash ? SCRIPT_VERIFY_P2SH : SCRIPT_VERIFY_NONE;

    // Start enforcing the DERSIG (BIP66) rule
    if (pindex->nHeight >= consensusparams.BIP66Height)
    {
        flags |= SCRIPT_VERIFY_DERSIG;
    }

    // Start enforcing CHECKLOCKTIMEVERIFY (BIP65) rule
    if (pindex->nHeight >= consensusparams.BIP65Height)
    {
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic.
    if (VersionBitsState(pindex->pprev, consensusparams, Consensus::DEPLOYMENT_CSV, versionbitscache) ==
        THRESHOLD_ACTIVE)
    {
        flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
    }

    // Start enforcing WITNESS rules using versionbits logic.
    if (IsWitnessEnabled(pindex->pprev, consensusparams))
    {
        flags |= SCRIPT_VERIFY_WITNESS;
        flags |= SCRIPT_VERIFY_NULLDUMMY;
    }

    // If the sbtc fork is enabled
    if (IsSBTCForkEnabled(consensusparams, pindex->pprev))
    {
        flags |= SCRIPT_ENABLE_SIGHASH_SBTC_FORK;
    }

    return flags;
}

bool CChainCommonent::ContextualCheckBlock(const CBlock &block, CValidationState &state,
                                           const Consensus::Params &consensusParams,
                                           const CBlockIndex *pindexPrev)
{
    const int nHeight = pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1;

    // Start enforcing BIP113 (Median Time Past) using versionbits logic.
    int nLockTimeFlags = 0;
    if (VersionBitsState(pindexPrev, consensusParams, Consensus::DEPLOYMENT_CSV, versionbitscache) == THRESHOLD_ACTIVE)
    {
        nLockTimeFlags |= LOCKTIME_MEDIAN_TIME_PAST;
    }

    int64_t nLockTimeCutoff = (nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST)
                              ? pindexPrev->GetMedianTimePast()
                              : block.GetBlockTime();

    // Check that all transactions are finalized
    for (const auto &tx : block.vtx)
    {
        // todu
        if (!tx->IsFinalTx(nHeight, nLockTimeCutoff))
        {
            return state.DoS(10, false, REJECT_INVALID, "bad-txns-nonfinal", false, "non-final transaction");
        }
    }

    // Enforce rule that the coinbase starts with serialized block height
    if (nHeight >= consensusParams.BIP34Height)
    {
        CScript expect = CScript() << nHeight;
        if (block.vtx[0]->vin[0].scriptSig.size() < expect.size() ||
            !std::equal(expect.begin(), expect.end(), block.vtx[0]->vin[0].scriptSig.begin()))
        {
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-height", false, "block height mismatch in coinbase");
        }
    }

    // Validation for witness commitments.
    // * We compute the witness hash (which is the hash including witnesses) of all the block's transactions, except the
    //   coinbase (where 0x0000....0000 is used instead).
    // * The coinbase scriptWitness is a stack of a single 32-byte vector, containing a witness nonce (unconstrained).
    // * We build a merkle tree with all those witness hashes as leaves (similar to the hashMerkleRoot in the block header).
    // * There must be at least one output whose scriptPubKey is a single 36-byte push, the first 4 bytes of which are
    //   {0xaa, 0x21, 0xa9, 0xed}, and the following 32 bytes are SHA256^2(witness root, witness nonce). In case there are
    //   multiple, the last one is used.
    bool fHaveWitness = false;
    if (VersionBitsState(pindexPrev, consensusParams, Consensus::DEPLOYMENT_SEGWIT, versionbitscache) ==
        THRESHOLD_ACTIVE)
    {
        int commitpos = GetWitnessCommitmentIndex(block);
        if (commitpos != -1)
        {
            bool malleated = false;
            uint256 hashWitness = BlockWitnessMerkleRoot(block, &malleated);
            // The malleation check is ignored; as the transaction tree itself
            // already does not permit it, it is impossible to trigger in the
            // witness tree.
            if (block.vtx[0]->vin[0].scriptWitness.stack.size() != 1 ||
                block.vtx[0]->vin[0].scriptWitness.stack[0].size() != 32)
            {
                return state.DoS(100, false, REJECT_INVALID, "bad-witness-nonce-size", true,
                                 strprintf("%s : invalid witness nonce size", __func__));
            }
            CHash256().Write(hashWitness.begin(), 32).Write(&block.vtx[0]->vin[0].scriptWitness.stack[0][0],
                                                            32).Finalize(hashWitness.begin());
            if (memcmp(hashWitness.begin(), &block.vtx[0]->vout[commitpos].scriptPubKey[6], 32))
            {
                return state.DoS(100, false, REJECT_INVALID, "bad-witness-merkle-match", true,
                                 strprintf("%s : witness merkle commitment mismatch", __func__));
            }
            fHaveWitness = true;
        }
    }

    // No witness data is allowed in blocks that don't commit to witness data, as this would otherwise leave room for spam
    if (!fHaveWitness)
    {
        for (const auto &tx : block.vtx)
        {
            if (tx->HasWitness())
            {
                return state.DoS(100, false, REJECT_INVALID, "unexpected-witness", true,
                                 strprintf("%s : unexpected witness data found", __func__));
            }
        }
    }

    // After the coinbase witness nonce and commitment are verified,
    // we can check if the block weight passes (before we've checked the
    // coinbase witness, it would be possible for the weight to be too
    // large by filling up the coinbase witness, which doesn't change
    // the block hash, so we couldn't mark the block as permanently
    // failed).
    if (GetBlockWeight(block) > MAX_BLOCK_WEIGHT)
    {
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-weight", false,
                         strprintf("%s : weight limit failed", __func__));
    }

    return true;
}

/** Apply the effects of this block (with given index) on the UTXO set represented by coins.
 *  Validity checks that depend on the UTXO set are also done; ConnectBlock()
 *  can fail if those validity checks fail (among other reasons).
*/
bool
CChainCommonent::ConnectBlock(const CBlock &block, CValidationState &state, CBlockIndex *pindex, CCoinsViewCache &view,
                              const CChainParams &chainparams, bool fJustCheck)
{
    AssertLockHeld(cs);
    assert(pindex);
    // pindex->phashBlock can be null if called by CreateNewBlock/TestBlockValidity
    assert((pindex->phashBlock == nullptr) || (*pindex->phashBlock == block.GetHash()));
    int64_t nTimeStart = GetTimeMicros();

    // Check it again in case a previous version let a bad block in
    if (!CheckBlock(block, state, chainparams.GetConsensus(), !fJustCheck, !fJustCheck))
        return error("%s: Consensus::CheckBlock: %s", __func__, FormatStateMessage(state));

    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = pindex->pprev == nullptr ? uint256() : pindex->pprev->GetBlockHash();
    assert(hashPrevBlock == view.GetBestBlock());

    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if (block.GetHash() == chainparams.GetConsensus().hashGenesisBlock)
    {
        if (!fJustCheck)
            view.SetBestBlock(pindex->GetBlockHash());
        return true;
    }

    bool fScriptChecks = true;
    if (!hashAssumeValid.IsNull())
    {
        // We've been configured with the hash of a block which has been externally verified to have a valid history.
        // A suitable default value is included with the software and updated from time to time.  Because validity
        //  relative to a piece of software is an objective fact these defaults can be easily reviewed.
        // This setting doesn't force the selection of any particular chain but makes validating some faster by
        //  effectively caching the result of part of the verification.
        BlockMap::const_iterator it = mapBlockIndex.find(hashAssumeValid);
        if (it != mapBlockIndex.end())
        {
            if (it->second->GetAncestor(pindex->nHeight) == pindex &&
                pindexBestHeader->GetAncestor(pindex->nHeight) == pindex &&
                pindexBestHeader->nChainWork >= nMinimumChainWork)
            {
                // This block is a member of the assumed verified chain and an ancestor of the best header.
                // The equivalent time check discourages hash power from extorting the network via DOS attack
                //  into accepting an invalid block through telling users they must manually set assumevalid.
                //  Requiring a software change or burying the invalid block, regardless of the setting, makes
                //  it hard to hide the implication of the demand.  This also avoids having release candidates
                //  that are hardly doing any signature verification at all in testing without having to
                //  artificially set the default assumed verified block further back.
                // The test against nMinimumChainWork prevents the skipping when denied access to any chain at
                //  least as good as the expected chain.
                fScriptChecks = (GetBlockProofEquivalentTime(*pindexBestHeader, *pindex, *pindexBestHeader,
                                                             chainparams.GetConsensus()) <= 60 * 60 * 24 * 7 * 2);
            }
        }
    }

    int64_t nTime1 = GetTimeMicros();
    nTimeCheck += nTime1 - nTimeStart;
    mlog.info("- Sanity checks: %.2fms [%.2fs]\n", 0.001 * (nTime1 - nTimeStart), nTimeCheck * 0.000001);

    // Do not allow blocks that contain transactions which 'overwrite' older transactions,
    // unless those are already completely spent.
    // If such overwrites are allowed, coinbases and transactions depending upon those
    // can be duplicated to remove the ability to spend the first instance -- even after
    // being sent to another address.
    // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
    // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
    // already refuses previously-known transaction ids entirely.
    // This rule was originally applied to all blocks with a timestamp after March 15, 2012, 0:00 UTC.
    // Now that the whole chain is irreversibly beyond that time it is applied to all blocks except the
    // two in the chain that violate it. This prevents exploiting the issue against nodes during their
    // initial block download.
    bool fEnforceBIP30 = (!pindex->phashBlock) || // Enforce on CreateNewBlock invocations which don't have a hash.
                         !((pindex->nHeight == 91842 && pindex->GetBlockHash() == uint256S(
                                 "0x00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec")) ||
                           (pindex->nHeight == 91880 && pindex->GetBlockHash() == uint256S(
                                   "0x00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721")));

    // Once BIP34 activated it was not possible to create new duplicate coinbases and thus other than starting
    // with the 2 existing duplicate coinbase pairs, not possible to create overwriting txs.  But by the
    // time BIP34 activated, in each of the existing pairs the duplicate coinbase had overwritten the first
    // before the first had been spent.  Since those coinbases are sufficiently buried its no longer possible to create further
    // duplicate transactions descending from the known pairs either.
    // If we're on the known chain at height greater than where BIP34 activated, we can save the db accesses needed for the BIP30 check.
    CBlockIndex *pindexBIP34height = pindex->pprev->GetAncestor(chainparams.GetConsensus().BIP34Height);
    //Only continue to enforce if we're below BIP34 activation height or the block hash at that height doesn't correspond.
    fEnforceBIP30 = fEnforceBIP30 && (!pindexBIP34height ||
                                      !(pindexBIP34height->GetBlockHash() == chainparams.GetConsensus().BIP34Hash));

    if (fEnforceBIP30)
    {
        for (const auto &tx : block.vtx)
        {
            for (size_t o = 0; o < tx->vout.size(); o++)
            {
                if (view.HaveCoin(COutPoint(tx->GetHash(), o)))
                {
                    return state.DoS(100, error("ConnectBlock(): tried to overwrite transaction"),
                                     REJECT_INVALID, "bad-txns-BIP30");
                }
            }
        }
    }

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic.
    int nLockTimeFlags = 0;
    if (VersionBitsState(pindex->pprev, chainparams.GetConsensus(), Consensus::DEPLOYMENT_CSV, versionbitscache) ==
        THRESHOLD_ACTIVE)
    {
        nLockTimeFlags |= LOCKTIME_VERIFY_SEQUENCE;
    }

    // Get the script flags for this block
    unsigned int flags = GetBlockScriptFlags(pindex, chainparams.GetConsensus());

    int64_t nTime2 = GetTimeMicros();
    nTimeForks += nTime2 - nTime1;
    mlog.info("    - Fork checks: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeForks * 0.000001);

    CBlockUndo blockundo;

    CCheckQueueControl<CScriptCheck> control(fScriptChecks && nScriptCheckThreads ? &scriptcheckqueue : nullptr);

    std::vector<int> prevheights;
    CAmount nFees = 0;
    int nInputs = 0;
    int64_t nSigOpsCost = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    std::vector<PrecomputedTransactionData> txdata;
    // Required so that pointers to individual PrecomputedTransactionData don't get invalidated
    txdata.reserve(block.vtx.size());
    ITxMempoolComponent *txmempool = (CTxMemPool *)appbase::CApp::Instance().FindComponent<ITxMempoolComponent>();
    for (unsigned int i = 0; i < block.vtx.size(); i++)
    {
        const CTransaction &tx = *(block.vtx[i]);

        //todo
        nInputs += tx.vin.size();

        if (!tx.IsCoinBase())
        {
            if (!view.HaveInputs(tx))
                return state.DoS(100, error("ConnectBlock(): inputs missing/spent"),
                                 REJECT_INVALID, "bad-txns-inputs-missingorspent");

            // Check that transaction is BIP68 final
            // BIP68 lock checks (as opposed to nLockTime checks) must
            // be in ConnectBlock because they require the UTXO set
            prevheights.resize(tx.vin.size());
            for (size_t j = 0; j < tx.vin.size(); j++)
            {
                prevheights[j] = view.AccessCoin(tx.vin[j].prevout).nHeight;
            }

            if (!tx.SequenceLocks(nLockTimeFlags, &prevheights, *pindex))
            {
                return state.DoS(100, error("%s: contains a non-BIP68-final transaction", __func__),
                                 REJECT_INVALID, "bad-txns-nonfinal");
            }
        }

        // GetTransactionSigOpCost counts 3 types of sigops:
        // * legacy (always)
        // * p2sh (when P2SH enabled in flags and excludes coinbase)
        // * witness (when witness enabled in flags and excludes coinbase)
        nSigOpsCost += tx.GetTransactionSigOpCost(view, flags);
        if (nSigOpsCost > MAX_BLOCK_SIGOPS_COST)
            return state.DoS(100, error("ConnectBlock(): too many sigops"),
                             REJECT_INVALID, "bad-blk-sigops");

        txdata.emplace_back(tx);
        if (!tx.IsCoinBase())
        {
            nFees += view.GetValueIn(tx) - tx.GetValueOut();

            std::vector<CScriptCheck> vChecks;
            bool fCacheResults = fJustCheck; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
            if (!tx.CheckInputs(state, view, fScriptChecks, flags, fCacheResults, fCacheResults, txdata[i],
                                nScriptCheckThreads ? &vChecks : nullptr))
                return error("ConnectBlock(): CheckInputs on %s failed with %s",
                             tx.GetHash().ToString(), FormatStateMessage(state));
            control.Add(vChecks);
        }

        CTxUndo undoDummy;
        if (i > 0)
        {
            blockundo.vtxundo.push_back(CTxUndo());
        }
        cViewManager.UpdateCoins(tx, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);

        vPos.push_back(std::make_pair(tx.GetHash(), pos));
        pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
    }
    int64_t nTime3 = GetTimeMicros();
    nTimeConnect += nTime3 - nTime2;
    mlog.debug("- Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n",
               (unsigned)block.vtx.size(), 0.001 * (nTime3 - nTime2), 0.001 * (nTime3 - nTime2) / block.vtx.size(),
               nInputs <= 1 ? 0 : 0.001 * (nTime3 - nTime2) / (nInputs - 1), nTimeConnect * 0.000001);

    CAmount blockReward = 0;
    blockReward = nFees + GetBlockSubsidy(pindex->nHeight, chainparams.GetConsensus());
    if (block.vtx[0]->GetValueOut() > blockReward)
        return state.DoS(100,
                         error("ConnectBlock(): coinbase pays too much (actual=%d vs limit=%d)",
                               block.vtx[0]->GetValueOut(), blockReward),
                         REJECT_INVALID, "bad-cb-amount");

    if (!control.Wait())
        return state.DoS(100, error("%s: CheckQueue failed", __func__), REJECT_INVALID, "block-validation-failed");
    int64_t nTime4 = GetTimeMicros();
    nTimeVerify += nTime4 - nTime2;
    mlog.debug("- Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1,
               0.001 * (nTime4 - nTime2), nInputs <= 1 ? 0 : 0.001 * (nTime4 - nTime2) / (nInputs - 1),
               nTimeVerify * 0.000001);

    if (fJustCheck)
        return true;

    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS))
    {
        // todo
        //        if (pindex->GetUndoPos().IsNull())
        //        {
        //            CDiskBlockPos _pos;
        //            if (!FindUndoPos(state, pindex->nFile, _pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
        //                return error("ConnectBlock(): FindUndoPos failed");
        //            if (!UndoWriteToDisk(blockundo, _pos, pindex->pprev->GetBlockHash(), chainparams.MessageStart()))
        //                return AbortNode(state, "Failed to write undo data");
        //
        //            // update nUndoPos in block index
        //            pindex->nUndoPos = _pos.nPos;
        //            pindex->nStatus |= BLOCK_HAVE_UNDO;
        //        }
        //
        //        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        //        setDirtyBlockIndex.insert(pindex);
    }

    if (fTxIndex)
        if (!pblocktree->WriteTxIndex(vPos))
            return AbortNode(state, "Failed to write transaction index");

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    int64_t nTime5 = GetTimeMicros();
    nTimeIndex += nTime5 - nTime4;
    mlog.debug("    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime5 - nTime4), nTimeIndex * 0.000001);

    int64_t nTime6 = GetTimeMicros();
    nTimeCallbacks += nTime6 - nTime5;
    mlog.debug("    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime6 - nTime5), nTimeCallbacks * 0.000001);
    return true;
}

/**
 * Connect a new block to chainActive. pblock is either nullptr or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 *
 * The block is added to connectTrace if connection succeeds.
 */
bool CChainCommonent::ConnectTip(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pIndexNew,
                                 const std::shared_ptr<const CBlock> &pblock, ConnectTrace &connectTrace,
                                 DisconnectedBlockTransactions &disconnectpool)
{
    assert(pIndexNew->pprev == Tip());
    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    std::shared_ptr<const CBlock> pthisBlock;
    if (!pblock)
    {
        std::shared_ptr<CBlock> pblockNew = std::make_shared<CBlock>();
        if (!ReadBlockFromDisk(*pblockNew, pIndexNew, chainparams.GetConsensus()))
            return AbortNode(state, "Failed to read block");
        pthisBlock = pblockNew;
    } else
    {
        pthisBlock = pblock;
    }
    const CBlock &blockConnecting = *pthisBlock;
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros();
    nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    mlog.debug("- Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001,
               nTimeReadFromDisk * 0.000001);
    {
        CCoinsViewCache view(cViewManager.GetCoinsTip());
        bool rv = ConnectBlock(blockConnecting, state, pIndexNew, view, chainparams);
        GetMainSignals().BlockChecked(blockConnecting, state);
        if (!rv)
        {
            if (state.IsInvalid())
                cIndexManager.InvalidBlockFound(pIndexNew, state);
            return error("ConnectTip(): ConnectBlock %s failed", pIndexNew->GetBlockHash().ToString());
        }
        nTime3 = GetTimeMicros();
        nTimeConnectTotal += nTime3 - nTime2;
        mlog.debug("  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001,
                   nTimeConnectTotal * 0.000001);
        bool flushed = view.Flush();
        assert(flushed);
    }
    int64_t nTime4 = GetTimeMicros();
    nTimeFlush += nTime4 - nTime3;
    mlog.debug("  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED, chainparams))
        return false;
    int64_t nTime5 = GetTimeMicros();
    nTimeChainState += nTime5 - nTime4;
    mlog.debug("  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001,
               nTimeChainState * 0.000001);

    // Remove conflicting transactions from the mempool.;
    ITxMempoolComponent *txmempool = (CTxMemPool *)appbase::CApp::Instance().FindComponent<ITxMempoolComponent>();
    //    txmempool->removeForBlock(blockConnecting.vtx, pIndexNew->nHeight); todo
    disconnectpool.removeForBlock(blockConnecting.vtx);
    // Update chainActive & related variables.
    UpdateTip(pIndexNew, chainparams);

    int64_t nTime6 = GetTimeMicros();
    nTimePostConnect += nTime6 - nTime5;
    nTimeTotal += nTime6 - nTime1;
    mlog.debug("  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001,
               nTimePostConnect * 0.000001);
    mlog.debug("- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);

    connectTrace.BlockConnected(pIndexNew, std::move(pthisBlock));
    return true;
}

void CChainCommonent::CheckForkWarningConditions()
{
    AssertLockHeld(cs);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before finishing our initial sync)
    if (IsInitialBlockDownload())
        return;

    cIndexManager.CheckForkWarningConditions();
}

bool CChainCommonent::ActivateBestChainStep(CValidationState &state, const CChainParams &chainparams,
                                            CBlockIndex *pIndexMostWork,
                                            const std::shared_ptr<const CBlock> &pblock, bool &bInvalidFound,
                                            ConnectTrace &connectTrace)
{
    AssertLockHeld(cs);
    const CBlockIndex *pIndexOldTip = Tip();
    const CBlockIndex *pIndexFork = cIndexManager.GetChain().FindFork(pIndexMostWork);

    ITxMempoolComponent *txmempool = (CTxMemPool *)appbase::CApp::Instance().FindComponent<ITxMempoolComponent>();

    // Disconnect active blocks which are no longer in the best chain.
    bool bBlocksDisconnected = false;
    DisconnectedBlockTransactions disconnectPool;
    while (Tip() && Tip() != pIndexFork)
    {
        if (!DisconnectTip(state, chainparams, &disconnectPool))
        {
            // This is likely a fatal error, but keep the mempool consistent,
            // just in case. Only remove from the mempool in this case.
            txmempool->UpdateMempoolForReorg(disconnectPool, false);
            return false;
        }
        bBlocksDisconnected = true;
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex *> vecIndexToConnect;
    bool bContinue = true;
    int iHeight = pIndexFork ? pIndexFork->nHeight : -1;
    while (bContinue && iHeight != pIndexMostWork->nHeight)
    {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int iTargetHeight = std::min(iHeight + 32, pIndexMostWork->nHeight);
        vecIndexToConnect.clear();
        vecIndexToConnect.reserve(iTargetHeight - iHeight);
        CBlockIndex *pIndexIter = pIndexMostWork->GetAncestor(iTargetHeight);
        while (pIndexIter && pIndexIter->nHeight != iHeight)
        {
            vecIndexToConnect.push_back(pIndexIter);
            pIndexIter = pIndexIter->pprev;
        }
        iHeight = iTargetHeight;

        // Connect new blocks.
        for (CBlockIndex *pIndexConnect : reverse_iterate(vecIndexToConnect))
        {
            if (!ConnectTip(state, chainparams, pIndexConnect,
                            pIndexConnect == pIndexMostWork ? pblock : std::shared_ptr<const CBlock>(), connectTrace,
                            disconnectPool))
            {
                if (state.IsInvalid())
                {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                    {
                        cIndexManager.InvalidChainFound(vecIndexToConnect.back());
                    }
                    state = CValidationState();
                    bInvalidFound = true;
                    bContinue = false;
                    break;
                } else
                {
                    // A system error occurred (disk space, database error, ...).
                    // Make the mempool consistent with the current tip, just in case
                    // any observers try to use it before shutdown.
                    //                    UpdateMempoolForReorg(disconnectpool, false);
                    txmempool->UpdateMempoolForReorg(disconnectPool, false);
                    return false;
                }
            } else
            {
                cIndexManager.PruneBlockIndexCandidates();
                if (!pIndexOldTip || Tip()->nChainWork > pIndexOldTip->nChainWork)
                {
                    // We're in a better position than we were. Return temporarily to release the lock.
                    bContinue = false;
                    break;
                }
            }
        }
    }

    if (bBlocksDisconnected)
    {
        // If any blocks were disconnected, disconnectpool may be non empty.  Add
        // any disconnected transactions back to the mempool.
        txmempool->UpdateMempoolForReorg(disconnectPool, true);
    }

    mempool.check(cViewManager.GetCoinsTip());

    // Callbacks/notifications for a new best chain.
    if (bInvalidFound)
    {
        cIndexManager.CheckForkWarningConditionsOnNewFork(vecIndexToConnect.back());
    } else
    {
        CheckForkWarningConditions();
    }

    return true;
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either nullptr or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool CChainCommonent::ActivateBestChain(CValidationState &state, const CChainParams &chainparams,
                                        std::shared_ptr<const CBlock> pblock)
{
    // Note that while we're often called here from ProcessNewBlock, this is
    // far from a guarantee. Things in the P2P/RPC will often end up calling
    // us in the middle of ProcessNewBlock - do not assume pblock is set
    // sanely for performance or correctness!

    CBlockIndex *pIndexMostWork = nullptr;
    CBlockIndex *pIndexNewTip = nullptr;

    const CChainParams &params = app().GetChainParams();
    const CArgsManager &appArgs = app().GetArgsManager();

    int iStopAtHeight = appArgs.GetArg<int>("-stopatheight", DEFAULT_STOPATHEIGHT);
    do
    {
        boost::this_thread::interruption_point();
        if (ShutdownRequested())
            break;

        const CBlockIndex *pIndexFork;
        bool bInitialDownload;
        {
            LOCK(cs);
            ConnectTrace connectTrace(mempool);

            CBlockIndex *pIndexOldTip = Tip();
            if (pIndexMostWork == nullptr)
            {
                pIndexMostWork = cIndexManager.FindMostWorkIndex();
            }

            // Whether we have anything to do at all.
            if (pIndexMostWork == nullptr || pIndexMostWork == pIndexOldTip)
            {
                return true;
            }

            bool bInvalidFound = false;
            std::shared_ptr<const CBlock> nullBlockPtr;
            if (!ActivateBestChainStep(state, chainparams, pIndexMostWork,
                                       pblock && pblock->GetHash() == pIndexMostWork->GetBlockHash() ? pblock
                                                                                                     : nullBlockPtr,
                                       bInvalidFound, connectTrace))
            {
                return false;
            }

            if (bInvalidFound)
            {
                // Wipe cache, we may need another branch now.
                pIndexMostWork = nullptr;
            }
            pIndexNewTip = Tip();
            pIndexFork = cIndexManager.GetChain().FindFork(pIndexOldTip);
            bInitialDownload = IsInitialBlockDownload();

            // block connect event
            for (const PerBlockConnectTrace &trace : connectTrace.GetBlocksConnected())
            {
                assert(trace.pblock && trace.pindex);
                GetMainSignals().BlockConnected(trace.pblock, trace.pindex, *trace.conflictedTxs);
            }
        }
        // When we reach this point, we switched to a new tip (stored in pindexNewTip).

        // Notifications/callbacks that can run without cs_main

        // Notify external listeners about the new tip.
        GetMainSignals().UpdatedBlockTip(pIndexNewTip, pIndexFork, bInitialDownload);

        // Always notify the UI if a new block tip was connected
        if (pIndexFork != pIndexNewTip)
        {
            uiInterface.NotifyBlockTip(bInitialDownload, pIndexNewTip);
        }

        if (iStopAtHeight && pIndexNewTip && pIndexNewTip->nHeight >= iStopAtHeight)
            StartShutdown();
    } while (pIndexNewTip != pIndexMostWork);

    cIndexManager.CheckBlockIndex(params.GetConsensus());
    return true;
}

bool CChainCommonent::InvalidateBlock(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pindex)
{
    AssertLockHeld(cs);

    // We first disconnect backwards and then mark the blocks as invalid.
    // This prevents a case where pruned nodes may fail to invalidateblock
    // and be left unable to start as they have no tip candidates (as there
    // are no blocks that meet the "have data and are not invalid per
    // nStatus" criteria for inclusion in setBlockIndexCandidates).

    bool pindex_was_in_chain = false;
    CChain chainActive = cIndexManager.GetChain();

    CBlockIndex *invalid_walk_tip = chainActive.Tip();

    CTxMemPool *txmempool = (CTxMemPool *)appbase::CApp::Instance().FindComponent<CTxMemPool>();

    DisconnectedBlockTransactions disconnectpool;
    while (chainActive.Contains(pindex))
    {
        pindex_was_in_chain = true;
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state, chainparams, &disconnectpool))
        {
            // It's probably hopeless to try to make the mempool consistent
            // here if DisconnectTip failed, but we can try.
            txmempool->UpdateMempoolForReorg(disconnectpool, false);
            return false;
        }
    }

    // Now mark the blocks we just disconnected as descendants invalid
    // (note this may not be all descendants).
    cIndexManager.InvalidateBlock(pindex, invalid_walk_tip, pindex_was_in_chain);

    // DisconnectTip will add transactions to disconnectpool; try to add these
    // back to the mempool.
    txmempool->UpdateMempoolForReorg(disconnectpool, true);

    cIndexManager.InvalidChainFound(pindex);
    uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindex->pprev);
    return true;
}

bool CChainCommonent::CheckActiveChain(CValidationState &state, const CChainParams &chainparams)
{
    LOCK(cs);

    CBlockIndex *pOldTipIndex = Tip();  // 1. current block chain tip
    mlog.info("Current tip block:%s\n", pOldTipIndex->ToString().c_str());
    MapCheckpoints checkpoints = chainparams.Checkpoints().mapCheckpoints;

    if (checkpoints.rbegin()->first < 1)
        return true;

    CChain chainActive = cIndexManager.GetChain();
    if (chainActive.Height() >= checkpoints.rbegin()->first &&
        chainActive[checkpoints.rbegin()->first]->GetBlockHash() == checkpoints.rbegin()->second)
    {
        return true;
    }


    auto GetFirstCheckPointInChain = [&]()
    {
        auto itReturn = checkpoints.rbegin();
        for (; itReturn != checkpoints.rend(); itReturn++)
        {
            if (chainActive[itReturn->first] != nullptr &&
                chainActive[itReturn->first]->GetBlockHash() == itReturn->second)
            {
                break;
            }
        }
        return itReturn;
    };

    auto GetFirstCheckPointNotInChain = [&](MapCheckpoints::const_reverse_iterator firstInChain)
    {
        assert(firstInChain != checkpoints.rbegin());
        firstInChain--;
        assert(chainActive[firstInChain->first] == nullptr ||
               chainActive[firstInChain->first]->GetBlockHash() != firstInChain->second);
        return firstInChain;
    };

    auto firstInChainPoint = GetFirstCheckPointInChain();
    assert(checkpoints.rbegin() != firstInChainPoint);
    auto firstNotInChainPoint = GetFirstCheckPointNotInChain(firstInChainPoint);


    if (firstNotInChainPoint->first > chainActive.Height())
    {
        return true;
    }

    CBlockIndex *desPoint = chainActive[firstNotInChainPoint->first];


    if (!InvalidateBlock(state, chainparams, desPoint))
    {
        return false;
    }

    if (state.IsValid())
    {
        ActivateBestChain(state, chainparams, nullptr);
    }

    if (!state.IsValid())
    {

        mlog.error("reject reason %s", state.GetRejectReason());
        return false;

    }


    if (chainActive.Tip() != pOldTipIndex)
    {
        // Write changes  to disk.
        if (!FlushStateToDisk(state, FLUSH_STATE_ALWAYS, chainparams))
        {
            return false;
        }
        uiInterface.NotifyBlockTip(IsInitialBlockDownload(), chainActive.Tip());
    }

    mlog.info("CheckActiveChain End====");

    return true;
}

bool CChainCommonent::RewindBlock(const CChainParams &params)
{
    LOCK(cs);

    // Note that during -reindex-chainstate we are called with an empty chainActive!

    int iHeight = 1;
    CChain chainActive = cIndexManager.GetChain();
    while (iHeight <= chainActive.Height())
    {
        if (cIndexManager.NeedRewind(iHeight, params.GetConsensus()))
        {
            break;
        }
        iHeight++;
    }

    // nHeight is now the height of the first insufficiently-validated block, or tipheight + 1
    CValidationState state;
    CBlockIndex *pIndex = chainActive.Tip();
    while (chainActive.Height() >= iHeight)
    {
        if (fPruneMode && !(chainActive.Tip()->nStatus & BLOCK_HAVE_DATA))
        {
            // If pruning, don't try rewinding past the HAVE_DATA point;
            // since older blocks can't be served anyway, there's
            // no need to walk further, and trying to DisconnectTip()
            // will fail (and require a needless reindex/redownload
            // of the blockchain).
            break;
        }
        if (!DisconnectTip(state, params, nullptr))
        {
            return error("RewindBlock: unable to disconnect block at height %i", pIndex->nHeight);
        }
        // Occasionally flush state to disk.
        if (!FlushStateToDisk(state, FLUSH_STATE_PERIODIC, params))
        {
            return false;
        }
    }

    // Reduce validity flag and have-data flags.
    // We do this after actual disconnecting, otherwise we'll end up writing the lack of data
    // to disk before writing the chainstate, resulting in a failure to continue if interrupted.
    cIndexManager.RewindBlockIndex(params.GetConsensus());

    if (Tip() != nullptr)
    {
        if (!FlushStateToDisk(state, FLUSH_STATE_ALWAYS, params))
        {
            return false;
        }
    }

    return true;
}

bool CChainCommonent::LoadChainTip(const CChainParams &chainparams)
{
    CChain chainActive = cIndexManager.GetChain();
    uint256 bestHash = cViewManager.GetCoinsTip()->GetBestBlock();
    if (chainActive.Tip() && chainActive.Tip()->GetBlockHash() == bestHash)
    {
        return true;
    }

    if (bestHash.IsNull() && cIndexManager.IsOnlyGenesisBlockIndex())
    {
        // In case we just added the genesis block, connect it now, so
        // that we always have a chainActive.Tip() when we return.
        mlog.info("%s: Connecting genesis block...", __func__);
        CValidationState state;
        if (!ActivateBestChain(state, chainparams, nullptr))
        {
            return false;
        }
    }

    // Load pointer to end of best chain
    CBlockIndex *pTip = cIndexManager.GetBlockIndex(bestHash);
    if (!pTip)
    {
        return false;
    }

    SetTip(pTip);

    cIndexManager.PruneBlockIndexCandidates();

    mlog.info("Loaded best chain: hashBestChain=%s height=%d date=%s progress=%f\n",
              chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(),
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
              GuessVerificationProgress(chainparams.TxData(), chainActive.Tip()));
    return true;
}

int CChainCommonent::VerifyBlocks()
{
    uiInterface.InitMessage(_("Verifying blocks..."));

    LOCK(cs);
    CBlockIndex *tip = Tip();
    if (tip && tip->nTime > GetAdjustedTime() + 2 * 60 * 60)
    {
        return ERR_FUTURE_BLOCK;
    }

    const CChainParams &chainParams = app().GetChainParams();
    const CArgsManager &cArgs = app().GetArgsManager();
    uint32_t checkLevel = cArgs.GetArg<uint32_t>("-checklevel", DEFAULT_CHECKLEVEL);
    int checkBlocks = cArgs.GetArg<int>("-checkblocks", DEFAULT_CHECKBLOCKS);
    if (VerifyDB(chainParams, cViewManager.GetCoinViewDB(), checkLevel, checkBlocks))
    {
        return ERR_VERIFY_DB;
    }

    return OK_CHAIN;
}

void CChainCommonent::ThreadImport()
{
    RenameThread("bitcoin-loadblk");

    const CChainParams &chainParams = app().GetChainParams();
    const CArgsManager &cArgs = app().GetArgsManager();

    if (bReIndex)
    {
        int iFile = 0;
        while (true)
        {
            CDiskBlockPos pos(iFile, 0);
            if (!fs::exists(GetBlockPosFilename(pos, "blk")))
                break; // No block files left to reindex
            FILE *file = OpenBlockFile(pos, true);
            if (!file)
                break; // This error is logged in OpenBlockFile
            mlog.info("Reindexing block file blk%05u.dat...", (unsigned int)iFile);
            LoadExternalBlockFile(chainParams, file, &pos);
            iFile++;
        }

        mlog.info("Reindexing finished");
    }

    if (cIndexManager.NeedInitGenesisBlock(chainParams))
    {
        if (!LoadGenesisBlock(chainParams))
        {
            return;
        }
    }

    // hardcoded $DATADIR/bootstrap.dat
    fs::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (fs::exists(pathBootstrap))
    {
        FILE *file = fsbridge::fopen(pathBootstrap, "rb");
        if (file)
        {
            fs::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            mlog.info("Importing bootstrap.dat...");
            LoadExternalBlockFile(chainParams, file, nullptr);
            RenameOver(pathBootstrap, pathBootstrapOld);
        } else
        {
            mlog.info("Warning: Could not open bootstrap file %s", pathBootstrap.string());
        }
    }

    for (const std::string &strFile : cArgs.GetArgs("-loadblock"))
    {
        FILE *file = fsbridge::fopen(strFile, "rb");
        if (file)
        {
            mlog.info("Importing blocks file %s...", strFile);
            LoadExternalBlockFile(chainParams, file, nullptr);
        } else
        {
            mlog.info("Warning: Could not open blocks file %s", strFile);
        }
    }

    // scan for better chains in the block chain database, that are not yet connected in the active best chain
    CValidationState state;
    if (!ActivateBestChain(state, chainParams, nullptr))
    {
        mlog.info("Failed to connect best block");
        return;
    }

    if (cArgs.GetArg<bool>("-stopafterblockimport", DEFAULT_STOPAFTERBLOCKIMPORT))
    {
        mlog.info("Stopping after block import");
        return;
    }
}

void CChainCommonent::NotifyHeaderTip()
{
    bool fNotify = false;
    bool fInitialBlockDownload = false;
    static CBlockIndex *pindexHeaderOld = nullptr;
    CBlockIndex *pindexHeader = nullptr;
    {
        LOCK(cs);
        pindexHeader = cIndexManager.GetIndexBestHeader();

        if (pindexHeader != pindexHeaderOld)
        {
            fNotify = true;
            fInitialBlockDownload = IsInitialBlockDownload();
            pindexHeaderOld = pindexHeader;
        }
    }
    // Send block tip changed notifications without cs_main
    if (fNotify)
    {
        uiInterface.NotifyHeaderTip(fInitialBlockDownload, pindexHeader);
    }
}

bool CChainCommonent::LoadExternalBlockFile(const CChainParams &chainParams, FILE *fileIn, CDiskBlockPos *dbp)
{
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    try
    {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2 * MAX_BLOCK_SERIALIZED_SIZE, MAX_BLOCK_SERIALIZED_SIZE + 8, SER_DISK,
                             CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof())
        {
            boost::this_thread::interruption_point();

            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try
            {
                // locate a header
                unsigned char buf[CMessageHeader::MESSAGE_START_SIZE];
                blkdat.FindByte(chainParams.MessageStart()[0]);
                nRewind = blkdat.GetPos() + 1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, chainParams.MessageStart(), CMessageHeader::MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > MAX_BLOCK_SERIALIZED_SIZE)
                    continue;
            } catch (const std::exception &)
            {
                // no valid block header found; don't complain
                break;
            }
            try
            {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
                CBlock &block = *pblock;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
                bool bParentNotFound = (cIndexManager.GetBlockIndex(block.hashPrevBlock) == nullptr) ? true : false;
                if (hash != chainParams.GetConsensus().hashGenesisBlock && bParentNotFound)
                {
                    mlog.info("%s: Out of order block %s, parent %s not known\n", __func__,
                              hash.ToString(),
                              block.hashPrevBlock.ToString());
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                if ((cIndexManager.GetBlockIndex(hash) == nullptr) ||
                    (cIndexManager.GetBlockIndex(hash)->nStatus & BLOCK_HAVE_DATA) == 0)
                {
                    LOCK(cs);
                    CValidationState state;
                    if (AcceptBlock(pblock, state, chainParams, nullptr, true, dbp, nullptr))
                        nLoaded++;
                    if (state.IsError())
                        break;
                } else if (hash != chainParams.GetConsensus().hashGenesisBlock &&
                           cIndexManager.GetBlockIndex(hash)->nHeight % 1000 == 0)
                {
                    mlog.info("Block Import: already had block %s at height %d", hash.ToString(),
                              mapBlockIndex[hash]->nHeight);
                }

                // Activate the genesis block so normal node progress can continue
                if (hash == chainParams.GetConsensus().hashGenesisBlock)
                {
                    CValidationState state;
                    if (!ActivateBestChain(state, chainParams, nullptr))
                    {
                        break;
                    }
                }

                NotifyHeaderTip();

                // Recursively process earlier encountered successors of this block
                std::deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty())
                {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator, std::multimap<uint256, CDiskBlockPos>::iterator> range = mapBlocksUnknownParent.equal_range(
                            head);
                    while (range.first != range.second)
                    {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
                        std::shared_ptr<CBlock> pblockrecursive = std::make_shared<CBlock>();
                        if (ReadBlockFromDisk(*pblockrecursive, it->second, chainParams.GetConsensus()))
                        {
                            mlog.info("%s: Processing out of order child %s of %s\n", __func__,
                                      pblockrecursive->GetHash().ToString(),
                                      head.ToString());
                            LOCK(cs);
                            CValidationState dummy;
                            if (AcceptBlock(pblockrecursive, dummy, chainParams, nullptr, true, &it->second, nullptr))
                            {
                                nLoaded++;
                                queue.push_back(pblockrecursive->GetHash());
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                        NotifyHeaderTip();
                    }
                }
            } catch (const std::exception &e)
            {
                mlog.error("%s: Deserialize or I/O error - %s", __func__, e.what());
            }
        }
    } catch (const std::runtime_error &e)
    {
        AbortNode(std::string("System error: ") + e.what());
    }
    if (nLoaded > 0)
        mlog.info("Loaded %i blocks from external file in %dms", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

/** Store block on disk. If dbp is non-nullptr, the file is known to already reside on disk */
bool CChainCommonent::AcceptBlock(const std::shared_ptr<const CBlock> &pblock, CValidationState &state,
                                  const CChainParams &chainparams, CBlockIndex **ppindex, bool fRequested,
                                  const CDiskBlockPos *dbp, bool *fNewBlock)
{
    const CBlock &block = *pblock;

    if (fNewBlock)
        *fNewBlock = false;
    AssertLockHeld(cs);

    CBlockIndex *pindexDummy = nullptr;
    CBlockIndex *&pindex = ppindex ? *ppindex : pindexDummy;

    if (!cIndexManager.AcceptBlockHeader(block, state, chainparams, &pindex))
        return false;

    // Try to process all requested blocks that we don't have, but only
    // process an unrequested block if it's new and has enough work to
    // advance our tip, and isn't too many blocks ahead.
    bool fAlreadyHave = pindex->nStatus & BLOCK_HAVE_DATA;
    CChain &chainActive = cIndexManager.GetChain();
    bool fHasMoreOrSameWork = (chainActive.Tip() ? pindex->nChainWork >= chainActive.Tip()->nChainWork : true);
    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip.  Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks.
    bool fTooFarAhead = (pindex->nHeight > int(chainActive.Height() + MIN_BLOCKS_TO_KEEP));

    // TODO: Decouple this function from the block download logic by removing fRequested
    // This requires some new chain data structure to efficiently look up if a
    // block is in a chain leading to a candidate for best tip, despite not
    // being such a candidate itself.

    // TODO: deal better with return value and error conditions for duplicate
    // and unrequested blocks.
    if (fAlreadyHave)
        return true;
    if (!fRequested)
    {  // If we didn't ask for it:
        if (pindex->nTx != 0)
            return true;    // This is a previously-processed block that was pruned
        if (!fHasMoreOrSameWork)
            return true; // Don't process less-work chains
        if (fTooFarAhead)
            return true;        // Block height is too high

        // Protect against DoS attacks from low-work chains.
        // If our tip is behind, a peer could try to send us
        // low-work blocks on a fake chain that we would never
        // request; don't process these.
        if (pindex->nChainWork < nMinimumChainWork)
            return true;
    }
    if (fNewBlock)
        *fNewBlock = true;

    if (!CheckBlock(block, state, chainparams.GetConsensus(), false, false) ||
        !ContextualCheckBlock(block, state, chainparams.GetConsensus(), pindex->pprev))
    {
        if (state.IsInvalid() && !state.CorruptionPossible())
        {
            pindex->nStatus |= BLOCK_FAILED_VALID;
            cIndexManager.SetDirtyIndex(pindex);
        }
        return error("%s: %s", __func__, FormatStateMessage(state));
    }

    // Header is valid/has work, merkle tree and segwit merkle tree are good...RELAY NOW
    // (but if it does not build on our best tip, let the SendMessages loop relay it)
    if (!IsInitialBlockDownload() && chainActive.Tip() == pindex->pprev)
        GetMainSignals().NewPoWValidBlock(pindex, pblock);

    int nHeight = pindex->nHeight;

    // Write block to history file
    try
    {
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        if (dbp != nullptr)
            blockPos = *dbp;
        if (!cIndexManager.FindBlockPos(state, blockPos, nBlockSize + 8, nHeight, block.GetBlockTime(), dbp != nullptr))
            return error("AcceptBlock(): FindBlockPos failed");
        if (dbp == nullptr)
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                AbortNode(state, "Failed to write block");
        if (!cIndexManager.ReceivedBlockTransactions(block, state, pindex, blockPos, chainparams.GetConsensus()))
            return error("AcceptBlock(): ReceivedBlockTransactions failed");
    } catch (const std::runtime_error &e)
    {
        return AbortNode(state, std::string("System error: ") + e.what());
    }

    //    if (fCheckForPruning) todo
    //        FlushStateToDisk(chainparams, state, FLUSH_STATE_NONE); // we just allocated more disk space for block files

    return true;
}

log4cpp::Category &CChainCommonent::mlog = log4cpp::Category::getInstance(EMTOSTR(CID_BLOCK_CHAIN));

bool CChainCommonent::AbortNode(const std::string &strMessage, const std::string &userMessage)
{
    SetMiscWarning(strMessage);
    mlog.error(strMessage);
    string message = userMessage.empty() ? _("Error: A fatal internal error occurred, see debug.log for details")
                                         : userMessage;
    mlog.error(message);
    uiInterface.ThreadSafeMessageBox(message, "", CClientUIInterface::MSG_ERROR);
    StartShutdown();
    return false;
}

bool CChainCommonent::AbortNode(CValidationState &state, const std::string &strMessage, const std::string &userMessage)
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

bool CChainCommonent::LoadCheckPoint()
{
    Checkpoints::CCheckPointDB cCheckPointDB;
    std::map<int, Checkpoints::CCheckData> values;

    const CChainParams &chainParams = app().GetChainParams();

    if (cCheckPointDB.LoadCheckPoint(values))
    {
        std::map<int, Checkpoints::CCheckData>::iterator it = values.begin();
        while (it != values.end())
        {
            Checkpoints::CCheckData data1 = it->second;
            chainParams.AddCheckPoint(data1.getHeight(), data1.getHash());
            it++;
        }
    }
    return true;
}

CBlockIndex *CChainCommonent::FindForkInGlobalIndex(const CChain &chain, const CBlockLocator &locator)
{
    return cIndexManager.FindForkInGlobalIndex(chain, locator);
}

log4cpp::Category &CChainCommonent::getLog()
{
    return mlog;
}

bool CChainCommonent::VerifyDB(const CChainParams &chainparams, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    uiInterface.ShowProgress(_("Verifying blocks..."), 0);

    CChain &chainActive = cIndexManager.GetChain();
    if (chainActive.Tip() == nullptr || chainActive.Tip()->pprev == nullptr)
        return true;

    // Verify blocks in the best chain
    if (nCheckDepth <= 0 || nCheckDepth > chainActive.Height())
        nCheckDepth = chainActive.Height();
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex *pindexState = chainActive.Tip();
    CBlockIndex *pindexFailure = nullptr;
    int nGoodTransactions = 0;
    CValidationState state;
    int reportDone = 0;
    LogPrintf("[0%%]...");
    for (CBlockIndex *pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev)
    {
        boost::this_thread::interruption_point();
        int percentageDone = std::max(1, std::min(99, (int)(((double)(chainActive.Height() - pindex->nHeight)) /
                                                            (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100))));
        if (reportDone < percentageDone / 10)
        {
            // report every 10% step
            LogPrintf("[%d%%]...", percentageDone);
            reportDone = percentageDone / 10;
        }
        uiInterface.ShowProgress(_("Verifying blocks..."), percentageDone);
        if (pindex->nHeight < chainActive.Height() - nCheckDepth)
            break;
        if (fPruneMode && !(pindex->nStatus & BLOCK_HAVE_DATA))
        {
            // If pruning, only go back as far as we have data.
            LogPrintf("VerifyDB(): block verification stopping at height %d (pruning, no data)\n", pindex->nHeight);
            break;
        }
        CBlock block;
        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus()))
            return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight,
                         pindex->GetBlockHash().ToString());
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state, chainparams.GetConsensus(), false, false))
            return error("%s: *** found bad block at %d, hash=%s (%s)\n", __func__,
                         pindex->nHeight, pindex->GetBlockHash().ToString(), FormatStateMessage(state));
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex)
        {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull())
            {
                if (!UndoReadFromDisk(undo, pos, pindex->pprev->GetBlockHash()))
                    return error("VerifyDB(): *** found bad undo data at %d, hash=%s\n", pindex->nHeight,
                                 pindex->GetBlockHash().ToString());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState &&
            (coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage()) <= nCoinCacheUsage)
        {
            assert(coins.GetBestBlock() == pindex->GetBlockHash());
            DisconnectResult res = cViewManager.DisconnectBlock(block, pindex, coins);
            if (res == DISCONNECT_FAILED)
            {
                return error("VerifyDB(): *** irrecoverable inconsistency in block data at %d, hash=%s",
                             pindex->nHeight, pindex->GetBlockHash().ToString());
            }
            pindexState = pindex->pprev;
            if (res == DISCONNECT_UNCLEAN)
            {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else
            {
                nGoodTransactions += block.vtx.size();
            }
        }
        if (ShutdownRequested())
            return true;
    }
    if (pindexFailure)
        return error(
                "VerifyDB(): *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n",
                chainActive.Height() - pindexFailure->nHeight + 1, nGoodTransactions);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4)
    {
        CBlockIndex *pindex = pindexState;
        while (pindex != chainActive.Tip())
        {
            boost::this_thread::interruption_point();
            uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, 100 - (int)(((double)(
                    chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * 50))));
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus()))
                return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight,
                             pindex->GetBlockHash().ToString());
            if (!ConnectBlock(block, state, pindex, coins, chainparams))
                return error("VerifyDB(): *** found unconnectable block at %d, hash=%s", pindex->nHeight,
                             pindex->GetBlockHash().ToString());
        }
    }

    LogPrintf("[DONE].\n");
    LogPrintf("No coin database inconsistencies in last %i blocks (%i transactions)\n",
              chainActive.Height() - pindexState->nHeight, nGoodTransactions);

    uiInterface.ShowProgress("", 100);

    return true;
}

// Exposed wrapper for AcceptBlockHeader
bool CChainCommonent::ProcessNewBlockHeaders(const std::vector<CBlockHeader> &headers, CValidationState &state,
                                             const CChainParams &chainparams, const CBlockIndex **ppindex,
                                             CBlockHeader *first_invalid)
{
    if (first_invalid != nullptr)
        first_invalid->SetNull();
    {
        LOCK(cs_main);
        for (const CBlockHeader &header : headers)
        {
            CBlockIndex *pindex = nullptr; // Use a temp pindex instead of ppindex to avoid a const_cast
            if (!cIndexManager.AcceptBlockHeader(header, state, chainparams, &pindex))
            {
                if (first_invalid)
                    *first_invalid = header;
                return false;
            }
            if (ppindex)
            {
                *ppindex = pindex;
            }
        }
    }
    NotifyHeaderTip();
    return true;
}

bool CChainCommonent::ProcessNewBlock(const CChainParams &chainparams, const std::shared_ptr<const CBlock> pblock,
                                      bool fForceProcessing,
                                      bool *fNewBlock)
{
    {
        CBlockIndex *pindex = nullptr;
        if (fNewBlock)
            *fNewBlock = false;
        CValidationState state;
        // Ensure that CheckBlock() passes before calling AcceptBlock, as
        // belt-and-suspenders.
        bool ret = CheckBlock(*pblock, state, chainparams.GetConsensus(), false, false);

        LOCK(cs_main);

        if (ret)
        {
            // Store to disk
            ret = AcceptBlock(pblock, state, chainparams, &pindex, fForceProcessing, nullptr, fNewBlock);
        }
        cIndexManager.CheckBlockIndex(chainparams.GetConsensus());
        if (!ret)
        {
            GetMainSignals().BlockChecked(*pblock, state);
            return error("%s: AcceptBlock FAILED", __func__);
        }
    }

    NotifyHeaderTip();

    CValidationState state; // Only used to report errors, not invalidity - ignore it
    if (!ActivateBestChain(state, chainparams, pblock))
        return error("%s: ActivateBestChain failed", __func__);

    return true;
}

bool CChainCommonent::TestBlockValidity(CValidationState &state, const CChainParams &chainparams, const CBlock &block,
                                        CBlockIndex *pindexPrev, bool fCheckPOW, bool fCheckMerkleRoot)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev && pindexPrev == chainActive.Tip());
    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!cIndexManager.ContextualCheckBlockHeader(block, state, chainparams, pindexPrev, GetAdjustedTime()))
        return error("%s: Consensus::ContextualCheckBlockHeader: %s", __func__, FormatStateMessage(state));
    if (!CheckBlock(block, state, chainparams.GetConsensus(), fCheckPOW, fCheckMerkleRoot))
        return error("%s: Consensus::CheckBlock: %s", __func__, FormatStateMessage(state));
    if (!ContextualCheckBlock(block, state, chainparams.GetConsensus(), pindexPrev))
        return error("%s: Consensus::ContextualCheckBlock: %s", __func__, FormatStateMessage(state));
    if (!ConnectBlock(block, state, &indexDummy, viewNew, chainparams, true))
        return false;
    assert(state.IsValid());

    return true;
}

/* This function is called from the RPC code for pruneblockchain */
void CChainCommonent::PruneBlockFilesManual(int nManualPruneHeight)
{
    //    CValidationState state;
    //    const CChainParams &chainparams = Params();
    //    FlushStateToDisk(chainparams, state, FLUSH_STATE_NONE, nManualPruneHeight);
}

bool CChainCommonent::PreciousBlock(CValidationState &state, const CChainParams &params, CBlockIndex *pindex)
{
    cIndexManager.PreciousBlock(state, params, pindex);

    GET_CHAIN_INTERFACE(ifChainObj);
    return ifChainObj->ActivateBestChain(state, params, nullptr);
}

bool CChainCommonent::ResetBlockFailureFlags(CBlockIndex *pindex)
{
    return cIndexManager.ResetBlockFailureFlags(pindex);
}