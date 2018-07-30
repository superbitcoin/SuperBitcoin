// Copyright (c) 2017 The Super The Super Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_BLOCKCHAIN_H
#define BITCOIN_RPC_BLOCKCHAIN_H

#include "framework/versionbits.h"

class CBlock;

class CBlockIndex;

class UniValue;

/**
 * Get the difficulty of the net wrt to the given block index, or the chain tip if
 * not provided.
 *
 * @return A floating point number that is a multiple of the main net minimum
 * difficulty (4295032833 hashes).
 */
double GetDifficulty(const CBlockIndex *blockindex = nullptr);

/** Callback for when block tip changed. */
void RPCNotifyBlockChange(bool ibd, const CBlockIndex *);

/** Block description to JSON */
UniValue blockToJSON(const CBlock &block, const CBlockIndex *blockindex, bool txDetails = false);

/** Mempool information to JSON */
UniValue mempoolInfoToJSON();

/** Mempool to JSON */
UniValue mempoolToJSON(bool fVerbose = false);

/** Block header to JSON */
UniValue blockheaderToJSON(const CBlockIndex *blockindex);

/** Get the BIP9 state for a given deployment at the current tip. */
ThresholdState VersionBitsTipState(const Consensus::Params &params, Consensus::DeploymentPos pos);

/** Get the numerical statistics for the BIP9 state for a given deployment at the current tip. */
BIP9Stats VersionBitsTipStatistics(const Consensus::Params &params, Consensus::DeploymentPos pos);

/** Get the block height at which the BIP9 deployment switched into the state for the block building on the current tip. */
int VersionBitsTipStateSinceHeight(const Consensus::Params &params, Consensus::DeploymentPos pos);

#endif

