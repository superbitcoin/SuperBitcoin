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
#include "sbtctransaction.h"
#include <libethereum/ChainParams.h>
#include <libethashseal/Ethash.h>
#include <libethashseal/GenesisInfo.h>
#include <script/standard.h>
#include "sbtccore/serialize.h"
#include <libethcore/Transaction.h>

#include "interface/icontractcomponent.h"


using valtype = std::vector<unsigned char>;
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

    bool CheckContractTx(const CTransaction tx, const CAmount nFees,
                         CAmount &nMinGasPrice, int &level,
                         string &errinfo, const CAmount nAbsurdFee = 0, bool rawTx = false) override;

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
                                bool bLogEvents,
                                bool fJustCheck,
                                std::map<dev::Address, std::pair<CHeightTxIndexKey, std::vector<uint256>>> &heightIndexes,
                                int &level, string &errinfo) override;

    void GetState(uint256 &hashStateRoot, uint256 &hashUTXORoot) override;

    void UpdateState(uint256 hashStateRoot, uint256 hashUTXORoot) override;

    void DeleteResults(std::vector<CTransactionRef> const &txs) override;

    std::vector<TransactionReceiptInfo> GetResult(uint256 const &hashTx) override;

    void CommitResults() override;

    void ClearCacheResult() override;

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