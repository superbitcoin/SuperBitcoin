///////////////////////////////////////////////////////////
//  minercomponent.cpp
//  Implementation of the Class CMinerComponent
//  Created on:      29-3-2018 09:57:57
//  Original author: marco
///////////////////////////////////////////////////////////
#include <boost/thread.hpp>

#include "util.h"
#include "minercomponent.h"
#include "miner.h"
#include "config/argmanager.h"
#include "config/chainparams.h"
#include "sbtccore/clientversion.h"
#include "p2p/net_processing.h"
#include "wallet/fees.h"
#include "compat/compat.h"
#include "interface/inetcomponent.h"
#include "interface/ichaincomponent.h"
#include "utils/utilmoneystr.h"
#include "framework/validationinterface.h"

SET_CPP_SCOPED_LOG_CATEGORY(CID_MINER);

static const bool DEFAULT_GENERATE = false;
static const int DEFAULT_GENERATE_THREADS = 1;

CMinerComponent::CMinerComponent()
{

}

CMinerComponent::~CMinerComponent()
{
}

bool CMinerComponent::ComponentInitialize()
{
    NLogStream() << "initialize CMinerComponent component";

    return true;
}

bool CMinerComponent::ComponentStartup()
{
    NLogStream() << "starting CMinerComponent component";
    // Generate coins in the background
    GenerateBitcoins(Args().GetArg<bool>("-gen", DEFAULT_GENERATE),
                     Args().GetArg<int>("-genproclimit", DEFAULT_GENERATE_THREADS), Params());
    return true;
}

bool CMinerComponent::ComponentShutdown()
{
    NLogStream() << "shutdown CMinerComponent component";

    GenerateBitcoins(false, 0, Params());
    return true;
}

void CMinerComponent::GenerateBitcoins(bool fGenerate, int nThreads, const CChainParams &chainparams)
{
    static boost::thread_group *minerThreads = NULL;

    if (nThreads < 0)
        nThreads = GetNumCores();

    if (minerThreads != NULL)
    {
        minerThreads->interrupt_all();
        delete minerThreads;
        minerThreads = NULL;
    }

    if (nThreads == 0 || !fGenerate)
        return;

    bool fRegTest = Args().IsArgSet("-regtest");
    bool fTestNet = Args().IsArgSet("-testnet");

    if (!fTestNet && !fRegTest)
        throw std::runtime_error("Error: start sbtc miner failed! only start sbtc miner in regtest or testnet.");

    minerThreads = new boost::thread_group();
    for (int i = 0; i < nThreads; i++)
        minerThreads->create_thread(boost::bind(&CMinerComponent::SbtcMiner, this, boost::cref(chainparams)));
}

void CMinerComponent::SbtcMiner(const CChainParams &chainparams)
{
    NLogStream() << "SbtcMiner started\n";
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("sbtc-miner");

    unsigned int nExtraNonce = 0;

    std::shared_ptr<CReserveScript> coinbaseScript;
    GetMainSignals().GetScriptForMining(coinbaseScript);

    try
    {
        // Throw an error if no script was provided.  This can happen
        // due to some internal error but also if the keypool is empty.
        // In the latter case, already the pointer is NULL.
        if (!coinbaseScript || coinbaseScript->reserveScript.empty())
            throw std::runtime_error("No coinbase script available (mining requires a wallet)");

        GET_NET_INTERFACE(ifNetObj);
        GET_CHAIN_INTERFACE(ifChainObj);
        GET_TXMEMPOOL_INTERFACE(ifTxMempoolObj);

        while (true)
        {
            if (chainparams.MiningRequiresPeers())
            {
                // Busy-wait for the network to come online so we don't waste time mining
                // on an obsolete chain. In regtest mode we expect to fly solo.
                do
                {
                    bool fvNodesEmpty;
                    {
                        fvNodesEmpty = (ifNetObj->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0);
                    }
                    if (!fvNodesEmpty)
                        break;
                    MilliSleep(1000);
                } while (true);
            }

            //
            // Create new block
            //
            unsigned int nTransactionsUpdatedLast = ifTxMempoolObj->GetMemPool().GetTransactionsUpdated();
            CBlockIndex *pindexPrev = ifChainObj->GetActiveChain().Tip();

            std::unique_ptr<CBlockTemplate> pblocktemplate(
                    BlockAssembler(Params()).CreateNewBlock(coinbaseScript->reserveScript));
            if (!pblocktemplate.get())
            {
                ELogFormat(
                        "Error in SbtcMiner: Keypool ran out, please call keypoolrefill before restarting the mining thread\n");
                return;
            }
            CBlock *pblock = &pblocktemplate->block;
            IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

            ILogFormat("Running SbtcMiner with %u transactions in block (%u bytes)\n", pblock->vtx.size(),
                       ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION));

            //
            // Search
            //
            int64_t nStart = GetTime();
            while (true)
            {
                if (CheckProofOfWork(pblock->GetHash(), pblock->nBits, Params().GetConsensus()))
                {
                    // Found a solution
                    SetThreadPriority(THREAD_PRIORITY_NORMAL);
                    ILogFormat("SbtcMiner:\n");
                    ILogFormat("proof-of-work found  \n  hash: %s  \n", pblock->GetHash().GetHex());
                    ProcessBlockFound(pblock, chainparams);
                    SetThreadPriority(THREAD_PRIORITY_LOWEST);
                    coinbaseScript->KeepScript();

                    // In regression test mode, stop mining after a block is found.
                    if (chainparams.MineBlocksOnDemand())
                        throw boost::thread_interrupted();

                    break;
                }

                ++pblock->nNonce;
                // Check for stop or if block needs to be rebuilt
                boost::this_thread::interruption_point();
                // Regtest mode doesn't require peers
                if ((ifNetObj->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0) && chainparams.MiningRequiresPeers())
                    break;
                if (pblock->nNonce >= 0xffff0000)
                    break;
                if (ifTxMempoolObj->GetMemPool().GetTransactionsUpdated() != nTransactionsUpdatedLast &&
                    GetTime() - nStart > 60)
                    break;
                if (pindexPrev != ifChainObj->GetActiveChain().Tip())
                    break;

                // Update nTime every few seconds
                if (UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev) < 0)
                    break; // Recreate the block if the clock has run backwards,

            }
        }
    }
    catch (const boost::thread_interrupted &)
    {
        NLogFormat("SbtcMiner terminated\n");
        throw;
    }
    catch (const std::runtime_error &e)
    {
        NLogFormat("SbtcMiner runtime error: %s\n", e.what());
        return;
    }
}

bool CMinerComponent::ProcessBlockFound(const CBlock *pblock, const CChainParams &chainparams)
{
    ILogFormat("%s\n", pblock->ToString());
    ILogFormat("generated %s\n", FormatMoney(pblock->vtx[0]->vout[0].nValue));

    GET_CHAIN_INTERFACE(ifChainObj);

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != ifChainObj->GetActiveChain().Tip()->GetBlockHash())
        {
            ELogFormat("SbtcMiner: generated block is stale");
            return false;
        }
    }

    // Inform about the new block
    GetMainSignals().BlockFound(pblock->GetHash());

    // Process this block the same as if we had received it from another node
    CValidationState state;
    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
    if (!ifChainObj->ProcessNewBlock(shared_pblock, true, NULL))
    {
        ELogFormat("SbtcMiner: ProcessNewBlock, block not accepted");
        return false;

    }

    return true;
}