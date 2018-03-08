///////////////////////////////////////////////////////////
//  mempoolcomponent.cpp
//  Implementation of the Class CMempoolComponent
//  Created on:      7-3-2018 16:40:57
//  Original author: marco
///////////////////////////////////////////////////////////

#include "mempoolcomponent.h"
#include "config/argmanager.h"
#include "sbtccore/clientversion.h"
#include "p2p/net_processing.h"
#include "wallet/fees.h"

log4cpp::Category &CMempoolComponent::mlog = log4cpp::Category::getInstance(EMTOSTR(CID_TX_MEMPOOL));

CMempoolComponent::CMempoolComponent()
{
    InitFeeEstimate();
    GetMemPool().SetEstimator(&feeEstimator);
}

CMempoolComponent::~CMempoolComponent()
{
}

bool CMempoolComponent::ComponentInitialize()
{
    std::cout << "initialize CTxMemPool component\n";
    if (Args().GetArg<bool>("-persistmempool", DEFAULT_PERSIST_MEMPOOL))
    {
        LoadMempool();
        bDumpMempoolLater = true;
    }
    return true;
}

bool CMempoolComponent::ComponentStartup()
{
    std::cout << "starting CTxMemPool component \n";
    return true;
}

bool CMempoolComponent::ComponentShutdown()
{
    std::cout << "shutdown CTxMemPool component \n";

    if (bDumpMempoolLater && Args().GetArg<bool>("-persistmempool", DEFAULT_PERSIST_MEMPOOL))
    {
        DumpMempool();
    }
    FlushFeeEstimate();
    return true;
}

CTxMemPool &CMempoolComponent::GetMemPool()
{
    return mempool;
}

bool CMempoolComponent::LoadMempool(void)
{
    const CChainParams &chainparams = Params();
    int64_t nExpiryTimeout = Args().GetArg<uint32_t>("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60;
    FILE *filestr = fsbridge::fopen(GetDataDir() / "mempool.dat", "rb");
    CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);
    if (file.IsNull())
    {
        mlog_notice("Failed to open mempool file from disk. Continuing anyway.\n");
        return false;
    }

    int64_t count = 0;
    int64_t skipped = 0;
    int64_t failed = 0;
    int64_t nNow = GetTime();

    try
    {
        uint64_t version;
        file >> version;
        if (version != MEMPOOL_DUMP_VERSION)
        {
            return false;
        }
        uint64_t num;
        file >> num;
        while (num--)
        {
            CTransactionRef tx;
            int64_t nTime;
            int64_t nFeeDelta;
            file >> tx;
            file >> nTime;
            file >> nFeeDelta;

            CAmount amountdelta = nFeeDelta;
            if (amountdelta)
            {
                mempool.PrioritiseTransaction(tx->GetHash(), amountdelta);
            }
            CValidationState state;
            if (nTime + nExpiryTimeout > nNow)
            {
                LOCK(cs);
                mempool.AcceptToMemoryPoolWithTime(chainparams, state, tx, true, nullptr, nTime, nullptr, false, 0);
                if (state.IsValid())
                {
                    ++count;
                } else
                {
                    ++failed;
                }
            } else
            {
                ++skipped;
            }
            if (app().ShutdownRequested())
                return false;
        }
        std::map<uint256, CAmount> mapDeltas;
        file >> mapDeltas;

        for (const auto &i : mapDeltas)
        {
            mempool.PrioritiseTransaction(i.first, i.second);
        }
    } catch (const std::exception &e)
    {
        mlog_notice("Failed to deserialize mempool data on disk: %s. Continuing anyway.\n", e.what());
        return false;
    }

    mlog_notice("Imported mempool transactions from disk: %i successes, %i failed, %i expired\n", count, failed,
                skipped);
    return true;
}

void CMempoolComponent::DumpMempool(void)
{
    int64_t start = GetTimeMicros();

    std::map<uint256, CAmount> mapDeltas;
    std::vector<TxMempoolInfo> vinfo;

    {
        LOCK(cs);
        for (const auto &i : mapDeltas)
        {
            mapDeltas[i.first] = i.second;
        }
        vinfo = mempool.infoAll();
    }

    int64_t mid = GetTimeMicros();

    try
    {
        FILE *filestr = fsbridge::fopen(GetDataDir() / "mempool.dat.new", "wb");
        if (!filestr)
        {
            return;
        }

        CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);

        uint64_t version = MEMPOOL_DUMP_VERSION;
        file << version;

        file << (uint64_t)vinfo.size();
        for (const auto &i : vinfo)
        {
            file << *(i.tx);
            file << (int64_t)i.nTime;
            file << (int64_t)i.nFeeDelta;
            mapDeltas.erase(i.tx->GetHash());
        }

        file << mapDeltas;
        FileCommit(file.Get());
        file.fclose();
        RenameOver(GetDataDir() / "mempool.dat.new", GetDataDir() / "mempool.dat");
        int64_t last = GetTimeMicros();
        mlog_notice("Dumped mempool: %gs to copy, %gs to dump\n", (mid - start) * 0.000001, (last - mid) * 0.000001);
    } catch (const std::exception &e)
    {
        mlog_notice("Failed to dump mempool: %s. Continuing anyway.\n", e.what());
    }
}

void CMempoolComponent::AddToCompactExtraTransactions(const CTransactionRef &tx)
{
    size_t max_extra_txn = app().GetArgsManager().GetArg<uint32_t>("-blockreconstructionextratxn",
                                                                   DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN);
    if (max_extra_txn <= 0)
        return;
    if (!vExtraTxnForCompact.size())
        vExtraTxnForCompact.resize(max_extra_txn);
    vExtraTxnForCompact[vExtraTxnForCompactIt] = std::make_pair(tx->GetWitnessHash(), tx);
    vExtraTxnForCompactIt = (vExtraTxnForCompactIt + 1) % max_extra_txn;
}

void CMempoolComponent::InitFeeEstimate()
{
    fs::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
    CAutoFile est_filein(fsbridge::fopen(est_path, "rb"), SER_DISK, CLIENT_VERSION);
    // Allowed to fail as this file IS missing on first startup.
    if (!est_filein.IsNull())
        ::feeEstimator.Read(est_filein);
    bFeeEstimatesInitialized = true;
}

void CMempoolComponent::FlushFeeEstimate()
{
    if (bFeeEstimatesInitialized)
    {
        ::feeEstimator.FlushUnconfirmed(mempool);
        fs::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
        CAutoFile est_fileout(fsbridge::fopen(est_path, "wb"), SER_DISK, CLIENT_VERSION);
        if (!est_fileout.IsNull())
            ::feeEstimator.Write(est_fileout);
        else
            LogPrintf("%s: Failed to write fee estimates to %s\n", __func__, est_path.string());
        bFeeEstimatesInitialized = false;
    }
}