// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "checkpoints.h"

#include "chain.h"
#include "chainparams.h"
#include "reverse_iterator.h"
#include "validation.h"
#include "uint256.h"
#include "key.h"
#include "utilstrencodings.h"
#include "util.h"
#include "streams.h"

#include <stdint.h>
#include <univalue/include/univalue.h>


namespace Checkpoints {


    CBlockIndex* GetLastCheckpoint(const CCheckpointData& data)
    {
        LOCK(cs_main);
        const MapCheckpoints& checkpoints = data.mapCheckpoints;

        for (const MapCheckpoints::value_type& i : reverse_iterate(checkpoints))
        {
            const uint256& hash = i.second;
            BlockMap::const_iterator t = mapBlockIndex.find(hash);
            if (t != mapBlockIndex.end())
                return t->second;
        }
        return nullptr;
    }



    CBlockIndex const * GetLastCheckPointBlockIndex(const CCheckpointData& data)
    {
        LOCK(cs_main);
        const MapCheckpoints& checkpoints = data.mapCheckpoints;
        auto lastItem = checkpoints.rbegin();

        for(auto it = lastItem; it != checkpoints.rend(); it++) {
            auto t = mapBlockIndex.find(it->second);
            if (t != mapBlockIndex.end())
                return t->second;
        }
        return nullptr;
    }


    bool GetCheckpointByHeight(const int nHeight, std::vector<CCheckData> &vnCheckPoints) {
        CCheckPointDB db;
        std::map<int, CCheckData> mapCheckPoint;
        if(db.LoadCheckPoint(mapCheckPoint))
        {
            auto iterMap = mapCheckPoint.upper_bound(nHeight);
            while (iterMap != mapCheckPoint.end()) {
                vnCheckPoints.push_back(iterMap->second);
                ++iterMap;
            }
        }
        return !vnCheckPoints.empty();
    }


    CCheckData::CCheckData() {
    }

    CCheckData::~CCheckData() {
    }

    bool CCheckData::CheckSignature(const CPubKey& cPubKey)  const{
        CDataStream data(SER_NETWORK, PROTOCOL_VERSION) ;
        data << height << hash;
        uint256 sighash = Hash(data.begin(), data.end());
        if (!cPubKey.Verify(sighash, m_vchSig)) {
            return  error("CCheckData::CheckSignature : verify signature failed");
        }
        return true;
    }

    bool CCheckData::Sign(const CKey &cKey) {
        CDataStream data(SER_NETWORK, PROTOCOL_VERSION) ;
        data << height << hash;
        uint256 sighash = Hash(data.begin(), data.end());
        if (!cKey.Sign(sighash, m_vchSig)) {
            return error("CCheckData::Sign: Unable to sign checkpoint, check private key?");
        }
        return true;
    }


    UniValue CCheckData::ToJsonObj() {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("height", height));
        obj.push_back(Pair("hash", hash.ToString()));
        obj.push_back(Pair("sig", HexStr(m_vchSig)));
        return obj;
    }

    int CCheckData::getHeight() const {
        return height;
    }

    void CCheckData::setHeight(int height) {
        CCheckData::height = height;
    }

    CCheckData::CCheckData(int hight, const uint256 &blockHash) : height(hight), hash(blockHash) {}

    const uint256 &CCheckData::getHash() const {
        return hash;
    }
    void CCheckData::setHash(const uint256 &blockHash) {
        CCheckData::hash = blockHash;
    }

    const std::vector<unsigned char> &CCheckData::getM_vchSig() const {
        return m_vchSig;
    }

    void CCheckData::setM_vchSig(const std::vector<unsigned char> &m_vchSig) {
        CCheckData::m_vchSig = m_vchSig;
    }


    CCheckPointDB::CCheckPointDB()  : db(GetDataDir() / ("checkpoint" + Params().NetworkIDString()),0)  {}

    bool CCheckPointDB::WriteCheckpoint(int height, const CCheckData &data) {
            return  db.Write(std::make_pair('c', height), data);
        }

    bool CCheckPointDB::ReadCheckpoint(int height, CCheckData &data) {
           return db.Read(std::make_pair('c', height), data);
    }

    bool CCheckPointDB::ExistCheckpoint(int height) {
        return  db.Exists(std::make_pair('c', height));
    }

    bool CCheckPointDB::LoadCheckPoint(std::map<int, CCheckData> &values) {
        std::unique_ptr<CDBIterator> pcursor(db.NewIterator());
        pcursor->Seek(std::make_pair('c',0));

        while (pcursor->Valid()) {
            std::pair<char, int > key;
            if (pcursor->GetKey(key) && key.first == 'c') {
                CCheckData data1;
                if (pcursor->GetValue(data1)) {
                    pcursor->Next();
                    values.insert(std::make_pair(data1.getHeight(),data1));
                } else {
                    return error("%s: failed to read value", __func__);
                }
            } else {
                break;
            }
        }

        return values.size() > 0;
    }


} // namespace Checkpoints
