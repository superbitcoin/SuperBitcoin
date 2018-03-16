// Copyright (c) 2009-2016 The Super Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "checkpoints.h"

#include "base/base.hpp"
#include "chain.h"
#include "chainparams.h"
#include "chaincomponent.h"
#include "blockindexmanager.h"
#include "reverse_iterator.h"
#include "block/validation.h"
#include "uint256.h"
#include "wallet/key.h"
#include "utils/utilstrencodings.h"
#include "utils/util.h"
#include "sbtccore/streams.h"

#include <stdint.h>
#include <univalue/include/univalue.h>

namespace Checkpoints
{
    bool GetCheckpointByHeight(const int nHeight, std::vector<CCheckData> &vnCheckPoints)
    {
        CCheckPointDB db;
        std::map<int, CCheckData> mapCheckPoint;
        if (db.LoadCheckPoint(mapCheckPoint))
        {
            auto iterMap = mapCheckPoint.upper_bound(nHeight);
            while (iterMap != mapCheckPoint.end())
            {
                vnCheckPoints.push_back(iterMap->second);
                ++iterMap;
            }
        }
        return !vnCheckPoints.empty();
    }

    CCheckData::CCheckData()
    {
    }

    CCheckData::~CCheckData()
    {
    }

    bool CCheckData::CheckSignature(const CPubKey &cPubKey) const
    {
        CDataStream data(SER_NETWORK, PROTOCOL_VERSION);
        data << height << hash;
        uint256 sighash = Hash(data.begin(), data.end());
        if (!cPubKey.Verify(sighash, m_vchSig))
        {
            mlog_error("CCheckData::CheckSignature : verify signature failed");
            return false;
        }
        return true;
    }

    bool CCheckData::Sign(const CKey &cKey)
    {
        CDataStream data(SER_NETWORK, PROTOCOL_VERSION);
        data << height << hash;
        uint256 sighash = Hash(data.begin(), data.end());
        if (!cKey.Sign(sighash, m_vchSig))
        {
            mlog_error("CCheckData::Sign: Unable to sign checkpoint, check private key?");
            return false;
        }
        return true;
    }

    UniValue CCheckData::ToJsonObj()
    {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("height", height));
        obj.push_back(Pair("hash", hash.ToString()));
        obj.push_back(Pair("sig", HexStr(m_vchSig)));
        return obj;
    }

    int CCheckData::getHeight() const
    {
        return height;
    }

    void CCheckData::setHeight(int height)
    {
        CCheckData::height = height;
    }

    CCheckData::CCheckData(int hight, const uint256 &blockHash) : height(hight), hash(blockHash)
    {
    }

    const uint256 &CCheckData::getHash() const
    {
        return hash;
    }

    void CCheckData::setHash(const uint256 &blockHash)
    {
        CCheckData::hash = blockHash;
    }

    const std::vector<unsigned char> &CCheckData::getM_vchSig() const
    {
        return m_vchSig;
    }

    void CCheckData::setM_vchSig(const std::vector<unsigned char> &m_vchSig)
    {
        CCheckData::m_vchSig = m_vchSig;
    }

    CCheckPointDB::CCheckPointDB() : db(GetDataDir() / ("checkpoint" + Params().NetworkIDString()), 0)
    {
    }

    bool CCheckPointDB::WriteCheckpoint(int height, const CCheckData &data)
    {
        return db.Write(std::make_pair('c', height), data);
    }

    bool CCheckPointDB::ReadCheckpoint(int height, CCheckData &data)
    {
        return db.Read(std::make_pair('c', height), data);
    }

    bool CCheckPointDB::ExistCheckpoint(int height)
    {
        return db.Exists(std::make_pair('c', height));
    }

    bool CCheckPointDB::LoadCheckPoint(std::map<int, CCheckData> &values)
    {
        std::unique_ptr<CDBIterator> pcursor(db.NewIterator());
        pcursor->Seek(std::make_pair('c', 0));

        while (pcursor->Valid())
        {
            std::pair<char, int> key;
            if (pcursor->GetKey(key) && key.first == 'c')
            {
                CCheckData data1;
                if (pcursor->GetValue(data1))
                {
                    pcursor->Next();
                    values.insert(std::make_pair(data1.getHeight(), data1));
                } else
                {
                    mlog_error("%s: failed to read value", __func__);
                    return false;
                }
            } else
            {
                break;
            }
        }

        return values.size() > 0;
    }

} // namespace Checkpoints

CBlockIndex *CBlockIndexManager::GetLastCheckpoint(const CCheckpointData &data)
{
    LOCK(cs);
    const MapCheckpoints &checkpoints = data.mapCheckpoints;

    for (const MapCheckpoints::value_type &i : reverse_iterate(checkpoints))
    {
        const uint256 &hash = i.second;
        auto t = GetBlockIndex(hash);
        if (t != nullptr)
            return t;
    }
    return nullptr;
}

CBlockIndex const *CBlockIndexManager::GetLastCheckPointBlockIndex(const CCheckpointData &data)
{
    LOCK(cs);
    const MapCheckpoints &checkpoints = data.mapCheckpoints;
    auto lastItem = checkpoints.rbegin();

    for (auto it = lastItem; it != checkpoints.rend(); it++)
    {
        auto t = GetBlockIndex(it->second);
        if (t != nullptr)
            return t;
    }
    return nullptr;
}

bool CBlockIndexManager::IsAgainstCheckPoint(const CChainParams &chainparams, const CBlockIndex *pindex)
{

    auto lastpioint = GetLastCheckPointBlockIndex(chainparams.Checkpoints());

    if (lastpioint == nullptr)
    {
        return false;
    }

    if (pindex->nHeight >= lastpioint->nHeight)
    {

        if (pindex->GetAncestor(lastpioint->nHeight)->GetBlockHash() == lastpioint->GetBlockHash())
        {
            return false;
        }

    } else
    {
        if (lastpioint->GetAncestor(pindex->nHeight)->GetBlockHash() == pindex->GetBlockHash())
        {
            return false;
        }
    }
    return true;
}

bool CBlockIndexManager::IsAgainstCheckPoint(const CChainParams &chainparams, const int &nHeight, const uint256 &hash)
{
    const auto tPoint = chainparams.Checkpoints();
    auto test = tPoint.mapCheckpoints.find(nHeight);
    if (test != tPoint.mapCheckpoints.end())
    {
        if (test->second != hash)
            return true;
    }
    return false;
}