// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHECKPOINTS_H
#define BITCOIN_CHECKPOINTS_H

#include "uint256.h"
#include "key.h"

#include <map>
#include <univalue/include/univalue.h>

class CBlockIndex;

struct CCheckpointData;

/**
 * Block-chain checkpoints are compiled-in sanity checks.
 * They are updated every release or three.
 */
namespace Checkpoints {

//! Returns last CBlockIndex* in mapBlockIndex that is a checkpoint
    CBlockIndex *GetLastCheckpoint(const CCheckpointData &data);


    class CCheckData {
    public:
        CCheckData();

        virtual ~CCheckData();

        bool CheckSignature(const CPubKey &cPubKey);


        bool Sign(const CKey &cPriKey, const std::vector<unsigned char> &vchSyncData);

        UniValue ToJsonObj();


        ADD_SERIALIZE_METHODS;

        template<typename Stream, typename Operation>
        inline void SerializationOp(Stream &s, Operation ser_action) {
            READWRITE(VARINT(hight));
            READWRITE(blockHash);
            READWRITE(m_vchSig);
        }


    public:
        int hight;
        uint256 blockHash;
        std::vector<unsigned char> m_vchSig;
    };


} //namespace Checkpoints

#endif // BITCOIN_CHECKPOINTS_H
