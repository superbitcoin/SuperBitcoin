// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHECKPOINTS_H
#define BITCOIN_CHECKPOINTS_H

#include "uint256.h"
#include "key.h"
#include "dbwrapper.h"

#include <map>
#include <univalue/include/univalue.h>

class CBlockIndex;

struct CCheckpointData;

/**
 * Block-chain checkpoints are compiled-in sanity checks.
 * They are updated every release or three.
 */
namespace Checkpoints {



    class CCheckData {
    public:
        CCheckData();

        CCheckData(int hight, const uint256 &blockHash);

        virtual ~CCheckData();

        bool CheckSignature(const CPubKey &cPubKey) const ;


        bool Sign(const CKey &cPriKey);

        UniValue ToJsonObj();


        ADD_SERIALIZE_METHODS;

        template<typename Stream, typename Operation>
        inline void SerializationOp(Stream &s, Operation ser_action) {
            READWRITE(VARINT(height));
            READWRITE(hash);
            READWRITE(m_vchSig);
        }

        int getHeight() const;

        void setHeight(int height);

        const uint256 &getHash() const;

        void setHash(const uint256 &blockHash);

        const std::vector<unsigned char> &getM_vchSig() const;

        void setM_vchSig(const std::vector<unsigned char> &m_vchSig);

    private:
        int height;
        uint256 hash;
        std::vector<unsigned char> m_vchSig;
    };
    class CCheckPointDB {
    public:
        CCheckPointDB();

        bool WriteCheckpoint(int height, const CCheckData& data);
        bool ReadCheckpoint(int height, CCheckData& data);
        bool ExistCheckpoint(int height);
        bool LoadCheckPoint(std::map<int, CCheckData>& values);

    protected:
        CDBWrapper db;
    };




//! Returns last CBlockIndex* in mapBlockIndex that is a checkpoint
    CBlockIndex *GetLastCheckpoint(const CCheckpointData &data);

    bool GetCheckpointByHeight(const int nHeight, std::vector<CCheckData> &vnCheckPoints);

    CBlockIndex const * GetLastCheckPointBlockIndex(const CCheckpointData& data);

} //namespace Checkpoints

#endif // BITCOIN_CHECKPOINTS_H
