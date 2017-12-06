// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include "consensus/params.h"

#include <stdint.h>

class CBlockHeader;
class CBlockIndex;
class uint256;
class arith_uint256;

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);

const CBlockIndex *GetSuitableBlock(const CBlockIndex *pindex);
unsigned int GetNextSBTCWorkRequired(const CBlockIndex *pindexPrev, const CBlockHeader *pblock, const Consensus::Params& params);
arith_uint256 ComputeTarget(const CBlockIndex *pindexFirst, const CBlockIndex *pindexLast, const Consensus::Params &params,int Curheight);

#endif // BITCOIN_POW_H
