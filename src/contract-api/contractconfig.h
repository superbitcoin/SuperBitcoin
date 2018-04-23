//
// Created by bille on 18-4-15.
//

#ifndef SUPERBITCOIN_CONTRACTCONFIG_H
#define SUPERBITCOIN_CONTRACTCONFIG_H

#include <utils/uint256.h>
#include <wallet/amount.h>

//contract executions with less gas than this are not standard
//Make sure is always equal or greater than MINIMUM_GAS_LIMIT (which we can't reference here due to insane header dependency chains)
static const uint64_t STANDARD_MINIMUM_GAS_LIMIT = 10000;
//contract executions with a price cheaper than this (in satoshis) are not standard
//TODO this needs to be controlled by DGP and needs to be propogated from consensus parameters
static const uint64_t STANDARD_MINIMUM_GAS_PRICE = 1;

static const uint64_t DEFAULT_GAS_LIMIT_OP_CREATE = 2500000;
static const uint64_t DEFAULT_GAS_LIMIT_OP_SEND = 250000;
static const CAmount DEFAULT_GAS_PRICE = 0.00000040 * COIN;
static const CAmount MAX_RPC_GAS_PRICE = 0.00000100 * COIN;


/** Minimum gas limit that is allowed in a transaction within a block - prevent various types of tx and mempool spam **/
static const uint64_t MINIMUM_GAS_LIMIT = 10000;

static const uint64_t MEMPOOL_MIN_GAS_LIMIT = 22000;

#define CONTRACT_STATE_DIR "stateContract"

static const uint256 DEFAULT_HASH_STATE_ROOT = uint256S(
        "0x9514771014c9ae803d8cea2731b2063e83de44802b40dce2d06acd02d0ff65e9");
static const uint256 DEFAULT_HASH_UTXO_ROOT = uint256S(
        "0x21b463e3b52f6201c0ad6c991be0485b6ef8c092e64583ffa655cc1b171fe856");

#endif //SUPERBITCOIN_CONTRACTCONFIG_H
