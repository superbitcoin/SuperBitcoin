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

    CCriticalSection cs_checkPoint;

    CBlockIndex* GetLastCheckpoint(const CCheckpointData& data)
    {
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


    bool GetCheckpointByHeight(const int nHeight, std::vector<CCheckData> &vnCheckPoints) {
        LOCK(cs_checkPoint);
        CCheckPointDB db;
        std::map<int, CCheckData> mapCheckPoint;
        if(db.LoadCheckPoint(mapCheckPoint))
        {
            std::map<int, CCheckData>::iterator iterMap = mapCheckPoint.upper_bound(nHeight);
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
//        CPubKey cPubKey(ParseHex(strPubKey));
        CDataStream data(SER_NETWORK, PROTOCOL_VERSION) ;
        data << hight << blockHash;
        if (!cPubKey.Verify(Hash(data.begin(), data.end()), m_vchSig)) {
            return  error("CCheckData::CheckSignature : verify signature failed");
        }
        return true;
    }

    bool CCheckData::Sign(const CKey &cKey) {
        CDataStream data(SER_NETWORK, PROTOCOL_VERSION) ;
        data << hight << blockHash;
        if (!cKey.Sign(Hash(data.begin(), data.end()), m_vchSig)) {
            return error("CCheckData::Sign: Unable to sign checkpoint, check private key?");
        }
        return true;
    }


    UniValue CCheckData::ToJsonObj() {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("hight", hight));
        obj.push_back(Pair("blockHash", blockHash.ToString()));
        obj.push_back(Pair("sig", HexStr(m_vchSig)));
        return obj;
    }

    int CCheckData::getHight() const {
        return hight;
    }

    void CCheckData::setHight(int hight) {
        CCheckData::hight = hight;
    }

    CCheckData::CCheckData(int hight, const uint256 &blockHash) : hight(hight), blockHash(blockHash) {}

    const uint256 &CCheckData::getBlockHash() const {
        return blockHash;
    }

    void CCheckData::setBlockHash(const uint256 &blockHash) {
        CCheckData::blockHash = blockHash;
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
                    values.insert(std::make_pair(data1.getHight(),data1));
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
