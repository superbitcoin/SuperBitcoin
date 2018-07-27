// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sbtccore/transaction/script/interpreter.h>
#include <utils/logger.h>
#include "block.h"

#include "hash.h"
#include "tinyformat.h"
#include "utils/utilstrencodings.h"
#include "crypto/common.h"

uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf(
            "CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
            GetHash().ToString(),
            nVersion,
            hashPrevBlock.ToString(),
            hashMerkleRoot.ToString(),
            nTime, nBits, nNonce,
            vtx.size());
    for (const auto &tx : vtx)
    {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}

VM_STATE_ROOT CBlock::GetVMState(uint256 &hashStateRoot, uint256 &hashUTXORoot) const
{
    if (this->nVersion & (((uint32_t) 1) << VERSIONBITS_SBTC_CONTRACT))
    {
        assert(vtx.size() > 1);
        const CTransaction &tx = *(vtx[1]);  // 0
        assert(tx.IsCoinBase2() == true);

        int index = 0;
        unsigned int  i = 0;
        for (i = 0; i < tx.vout.size(); i++)
        {
            if (tx.vout[i].scriptPubKey.HasOpVmHashState())
            {
                index = i;
                break;
            }
        }

        if(i >= tx.vout.size())
        {  // must to have VmHashState vout
            assert(0);
            ELogFormat("Error: GetVMState coinbase vout");
            return RET_VM_STATE_ERR;
        }
        // have VmHashState vout
        std::vector<std::vector<unsigned char> > stack;
        EvalScript(stack, tx.vout[index].scriptPubKey, SCRIPT_EXEC_BYTE_CODE, BaseSignatureChecker(),
                   SIGVERSION_BASE, nullptr);
        if (stack.empty())
        {
            // VmHashState vout script err
            assert(0);
            ELogFormat("Error: GetVMState coinbase vout.scriptPubKey err");
            return RET_VM_STATE_ERR;
        }

        std::vector<unsigned char> code(stack.back());
        stack.pop_back();

        std::vector<unsigned char> vechashUTXORoot(stack.back());
        stack.pop_back();

        std::string strUTXO;
        strUTXO = HexStr(vechashUTXORoot);
        hashUTXORoot = uint256S(strUTXO);

        std::vector<unsigned char> vechashStateRoot(stack.back());
        stack.pop_back();

        std::string strHASH;
        strHASH = HexStr(vechashStateRoot);
        hashStateRoot = uint256S(strHASH);
        return RET_VM_STATE_OK;
    }else {
        return RET_CONTRACT_UNENBALE;
    }
}