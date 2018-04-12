///////////////////////////////////////////////////////////
//  contractcomponent.h
//  Created on:      19-3-2018 16:40:57
//  Original author: bille
///////////////////////////////////////////////////////////
#ifndef SUPERBITCOIN_CONTRACTCOMPONENT_H
#define SUPERBITCOIN_CONTRACTCOMPONENT_H

#pragma once

#include <stdint.h>
#include <log4cpp/Category.hh>
#include "util.h"

#include "orphantx.h"
#include "txmempool.h"

/////////////////////////////////////////// //sbtc-vm
#include "sbtcstate.h"
#include "sbtcDGP.h"
//#include "storageresults.h"
#include "sbtctransaction.h"
#include <libethereum/ChainParams.h>
#include <libethashseal/Ethash.h>
#include <libethashseal/GenesisInfo.h>
#include <script/standard.h>
#include "sbtccore/serialize.h"
#include <libethcore/Transaction.h>

#include "interface/icontractcomponent.h"

//contract executions with less gas than this are not standard
//Make sure is always equal or greater than MINIMUM_GAS_LIMIT (which we can't reference here due to insane header dependency chains)
static const uint64_t STANDARD_MINIMUM_GAS_LIMIT = 10000;
//contract executions with a price cheaper than this (in satoshis) are not standard
//TODO this needs to be controlled by DGP and needs to be propogated from consensus parameters
static const uint64_t STANDARD_MINIMUM_GAS_PRICE = 1;


using valtype = std::vector<unsigned char>;


static const uint64_t DEFAULT_GAS_LIMIT_OP_CREATE = 2500000;
static const uint64_t DEFAULT_GAS_LIMIT_OP_SEND = 250000;
static const CAmount DEFAULT_GAS_PRICE = 0.00000040 * COIN;
static const CAmount MAX_RPC_GAS_PRICE = 0.00000100 * COIN;


/** Minimum gas limit that is allowed in a transaction within a block - prevent various types of tx and mempool spam **/
static const uint64_t MINIMUM_GAS_LIMIT = 10000;

static const uint64_t MEMPOOL_MIN_GAS_LIMIT = 22000;

#define CONTRACT_STATE_DIR "stateContract"
///////////////////////////////////////////

struct EthTransactionParams
{
    VersionVM version;
    dev::u256 gasLimit;
    dev::u256 gasPrice;
    valtype code;
    dev::Address receiveAddress;

    bool operator!=(EthTransactionParams etp)
    {
        if (this->version.toRaw() != etp.version.toRaw() || this->gasLimit != etp.gasLimit ||
            this->gasPrice != etp.gasPrice || this->code != etp.code ||
            this->receiveAddress != etp.receiveAddress)
            return true;
        return false;
    }
};

using ExtractSbtcTX = std::pair<std::vector<SbtcTransaction>, std::vector<EthTransactionParams>>;


class SbtcTxConverter
{

public:

    SbtcTxConverter(CTransaction tx, CCoinsViewCache *v = NULL, const std::vector<CTransactionRef> *blockTxs = NULL)
            : txBit(tx), view(v), blockTransactions(blockTxs)
    {
    }

    bool extractionSbtcTransactions(ExtractSbtcTX &sbtcTx);

private:

    bool receiveStack(const CScript &scriptPubKey);

    bool parseEthTXParams(EthTransactionParams &params);

    SbtcTransaction createEthTX(const EthTransactionParams &etp, const uint32_t nOut);

    const CTransaction txBit;
    const CCoinsViewCache *view;
    std::vector<valtype> stack;
    opcodetype opcode;
    const std::vector<CTransactionRef> *blockTransactions;

};


class ByteCodeExec
{

public:

    ByteCodeExec(const CBlock &_block, std::vector<SbtcTransaction> _txs, const uint64_t _blockGasLimit) : txs(_txs),
                                                                                                           block(_block),
                                                                                                           blockGasLimit(
                                                                                                                   _blockGasLimit)
    {
    }

    bool performByteCode(dev::eth::Permanence type = dev::eth::Permanence::Committed);

    bool processingResults(ByteCodeExecResult &result);

    std::vector<ResultExecute> &getResult()
    {
        return result;
    }

private:

    dev::eth::EnvInfo BuildEVMEnvironment();

    dev::Address EthAddrFromScript(const CScript &scriptIn);

    std::vector<SbtcTransaction> txs;

    std::vector<ResultExecute> result;

    const CBlock &block;

    const uint64_t blockGasLimit;

};

class CContractComponent : public IContractComponent
{
public:
    CContractComponent();

    ~CContractComponent();

    bool ComponentInitialize() override;

    bool ContractInit() override;

    bool ComponentStartup() override;

    bool ComponentShutdown() override;

    uint64_t GetMinGasPrice(int height) override;

    uint64_t GetBlockGasLimit(int height) override;

    bool AddressInUse(string contractaddress) override;

    bool ChecckContractTx(const CTransaction tx, const CAmount nFees, CAmount &nMinGasPrice, int &level,
                          string &errinfo) override;

    bool RunContractTx(CTransaction tx, CCoinsViewCache *v, CBlock *pblock,
                       uint64_t minGasPrice,
                       uint64_t hardBlockGasLimit,
                       uint64_t softBlockGasLimit,
                       uint64_t txGasLimit,
                       uint64_t usedGas,
                       ByteCodeExecResult &testExecResult) override;

    bool ContractTxConnectBlock(CTransaction tx, uint32_t transactionIndex, CCoinsViewCache *v, const CBlock &block,
                                int nHeight,
                                ByteCodeExecResult &bcer,
                                StorageResults *pStorageRes,
                                bool fJustCheck,
                                std::map<dev::Address, std::pair<CHeightTxIndexKey, std::vector<uint256>>> &heightIndexes,
                                int &level, string &errinfo) override;

    void GetState(uint256 &hashStateRoot, uint256 &hashUTXORoot) override;

    void UpdateState(uint256 hashStateRoot, uint256 hashUTXORoot) override;

    std::map<dev::h256, std::pair<dev::u256, dev::u256>> GetStorageByAddress(string address) override;

    void SetTemporaryState(uint256 hashStateRoot, uint256 hashUTXORoot) override;

    std::unordered_map<dev::h160, dev::u256> GetContractList() override;

    CAmount GetContractBalance(dev::h160 address) override;

    std::vector<uint8_t> GetContractCode(dev::Address address) override;

    bool
    GetContractVin(dev::Address address, dev::h256 &hash, uint32_t &nVout, dev::u256 &value, uint8_t &alive) override;

    void
    RPCCallContract(UniValue &result, const string addrContract, std::vector<unsigned char> opcode, string sender = "",
                    uint64_t gasLimit = 0) override;

    string GetExceptedInfo(uint32_t index) override;

private:

};


#endif //SUPERBITCOIN_CONTRACTCOMPONENT_H