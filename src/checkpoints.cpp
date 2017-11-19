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


    CCheckData::CCheckData() {
    }

    CCheckData::~CCheckData() {
    }

    bool CCheckData::CheckSignature(const CPubKey& cPubKey) {
//        CPubKey cPubKey(ParseHex(strPubKey));
        CDataStream data(SER_NETWORK, PROTOCOL_VERSION) ;
        data << hight << blockHash;
        if (!cPubKey.Verify(Hash(data.begin(), data.end()), m_vchSig)) {
            return  error("CCheckData::CheckSignature : verify signature failed");
        }
        return true;
    }

    bool CCheckData::Sign(const CKey &cKey, const std::vector<unsigned char>& vchSyncData) {
        if (!cKey.Sign(Hash(vchSyncData.begin(), vchSyncData.end()), m_vchSig)) {
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


} // namespace Checkpoints
