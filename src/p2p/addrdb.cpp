// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Super Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "addrdb.h"

#include "p2p/addrman.h"
#include "sbtcd/baseimpl.hpp"
#include "config/chainparams.h"
#include "sbtccore/clientversion.h"
#include "fs.h"
#include "hash.h"
#include "random.h"
#include "sbtccore/streams.h"
#include "tinyformat.h"
#include "utils/util.h"

SET_CPP_SCOPED_LOG_CATEGORY(CID_P2P_NET);

namespace
{

    template<typename Stream, typename Data>
    bool SerializeDB(Stream &stream, const Data &data)
    {
        // Write and commit header, data
        try
        {
            CHashWriter hasher(SER_DISK, CLIENT_VERSION);
            stream << FLATDATA(Params().MessageStart()) << data;
            hasher << FLATDATA(Params().MessageStart()) << data;
            stream << hasher.GetHash();
        } catch (const std::exception &e)
        {
            return rLogError("%s: Serialize or I/O error - %s", __func__, e.what());
        }

        return true;
    }

    template<typename Data>
    bool SerializeFileDB(const std::string &prefix, const fs::path &path, const Data &data)
    {
        // Generate random temporary filename
        unsigned short randv = 0;
        GetRandBytes((unsigned char *)&randv, sizeof(randv));
        std::string tmpfn = strprintf("%s.%04x", prefix, randv);

        // open temp output file, and associate with CAutoFile
        fs::path pathTmp = GetDataDir() / tmpfn;
        FILE *file = fsbridge::fopen(pathTmp, "wb");
        CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
        if (fileout.IsNull())
        {
            return rLogError("%s: Failed to open file %s", __func__, pathTmp.string());
        }

        // Serialize
        if (!SerializeDB(fileout, data))
            return false;
        FileCommit(fileout.Get());
        fileout.fclose();

        // replace existing file, if any, with new file
        if (!RenameOver(pathTmp, path))
        {
            return rLogError("%s: Rename-into-place failed", __func__);
        }

        return true;
    }

    template<typename Stream, typename Data>
    bool DeserializeDB(Stream &stream, Data &data, bool fCheckSum = true)
    {
        try
        {
            CHashVerifier<Stream> verifier(&stream);
            // de-serialize file header (network specific magic number) and ..
            unsigned char pchMsgTmp[4];
            verifier >> FLATDATA(pchMsgTmp);
            // ... verify the network matches ours
            if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
            {
                return rLogError("%s: Invalid network magic number", __func__);
            }

            // de-serialize data
            verifier >> data;

            // verify checksum
            if (fCheckSum)
            {
                uint256 hashTmp;
                stream >> hashTmp;
                if (hashTmp != verifier.GetHash())
                {
                    return rLogError("%s: Checksum mismatch, data corrupted", __func__);
                }
            }
        }
        catch (const std::exception &e)
        {
            return rLogError("%s: Deserialize or I/O error - %s", __func__, e.what());
        }

        return true;
    }

    template<typename Data>
    bool DeserializeFileDB(const fs::path &path, Data &data)
    {
        // open input file, and associate with CAutoFile
        FILE *file = fsbridge::fopen(path, "rb");
        CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
        if (filein.IsNull())
        {
            return rLogError("%s: Failed to open file %s", __func__, path.string());
        }

        return DeserializeDB(filein, data);
    }

}

CBanDB::CBanDB()
{
    pathBanlist = GetDataDir() / "banlist.dat";
}

bool CBanDB::Write(const banmap_t &banSet)
{
    return SerializeFileDB("banlist", pathBanlist, banSet);
}

bool CBanDB::Read(banmap_t &banSet)
{
    return DeserializeFileDB(pathBanlist, banSet);
}

CAddrDB::CAddrDB()
{
    pathAddr = GetDataDir() / "peers.dat";
}

bool CAddrDB::Write(const CAddrMan &addr)
{
    return SerializeFileDB("peers", pathAddr, addr);
}

bool CAddrDB::Read(CAddrMan &addr)
{
    return DeserializeFileDB(pathAddr, addr);
}

bool CAddrDB::Read(CAddrMan &addr, CDataStream &ssPeers)
{
    bool ret = DeserializeDB(ssPeers, addr, false);
    if (!ret)
    {
        // Ensure addrman is left in a clean state
        addr.Clear();
    }
    return ret;
}
