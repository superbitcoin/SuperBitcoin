#pragma once

#include <univalue/include/univalue.h>
#include "base/base.hpp"
#include "componentid.h"
#include "framework/component.hpp"
#include "utils/uint256.h"
#include "sbtccore/transaction/transaction.h"
#include "wallet/amount.h"

#include "contract-api/contractbase.h"
#include "contract-api/storageresults.h"

class IContractComponent : public appbase::TComponent<IContractComponent>
{
public:
    virtual ~IContractComponent()
    {
    }

    enum
    {
        ID = CID_CONTRACT
    };

    virtual int GetID() const override
    {
        return ID;
    }

    virtual bool ComponentInitialize() = 0;

    virtual bool ContractInit() = 0;

    virtual bool ComponentStartup() = 0;

    virtual bool ComponentShutdown() = 0;

    virtual uint64_t GetMinGasPrice(int height) = 0;

    virtual uint64_t GetBlockGasLimit(int height) = 0;

    virtual bool AddressInUse(string contractaddress) = 0;

    virtual bool CheckContractTx(const CTransaction tx, const CAmount nFees,
                                 CAmount &nMinGasPrice, int &level,
                                 string &errinfo, const CAmount nAbsurdFee = 0, bool rawTx = false) = 0;

    virtual bool RunContractTx(CTransaction tx, CCoinsViewCache *v, CBlock *pblock,
                               uint64_t minGasPrice,
                               uint64_t hardBlockGasLimit,
                               uint64_t softBlockGasLimit,
                               uint64_t txGasLimit,
                               uint64_t usedGas,
                               ByteCodeExecResult &testExecResult) = 0;

    virtual bool
    ContractTxConnectBlock(CTransaction tx, uint32_t transactionIndex, CCoinsViewCache *v, const CBlock &block,
                           int nHeight,
                           ByteCodeExecResult &bcer,
                           bool bLogEvents,
                           bool fJustCheck,
                           std::map<dev::Address, std::pair<CHeightTxIndexKey, std::vector<uint256>>> &heightIndexes,
                           int &level, string &errinfo) = 0;

    virtual void GetState(uint256 &hashStateRoot, uint256 &hashUTXORoot) = 0;

    virtual void UpdateState(uint256 hashStateRoot, uint256 hashUTXORoot) = 0;

    virtual void DeleteResults(std::vector<CTransactionRef> const &txs) = 0;

    virtual std::vector<TransactionReceiptInfo> GetResult(uint256 const &hashTx) = 0;

    virtual void CommitResults() = 0;

    virtual void ClearCacheResult() = 0;

    virtual std::map<dev::h256, std::pair<dev::u256, dev::u256>> GetStorageByAddress(string address) = 0;

    virtual void SetTemporaryState(uint256 hashStateRoot, uint256 hashUTXORoot) = 0;

    virtual std::unordered_map<dev::h160, dev::u256> GetContractList() = 0;

    virtual CAmount GetContractBalance(dev::h160 address) = 0;

    virtual std::vector<uint8_t> GetContractCode(dev::Address address) = 0;

    virtual bool
    GetContractVin(dev::Address address, dev::h256 &hash, uint32_t &nVout, dev::u256 &value, uint8_t &alive) = 0;

    virtual void
    RPCCallContract(UniValue &result, const string addrContract, std::vector<unsigned char> opcode, string sender,
                    uint64_t gasLimit) = 0;

    virtual string GetExceptedInfo(uint32_t index) = 0;
    //add other interface methods here ...
};

#define GET_CONTRACT_INTERFACE(ifObj) \
    auto ifObj = GetApp()->FindComponent<IContractComponent>()
