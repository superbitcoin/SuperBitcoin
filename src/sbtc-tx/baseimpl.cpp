#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>
#include <univalue.h>
#include <thread>
#include <stdio.h>
#include <boost/algorithm/string.hpp>

#include "baseimpl.hpp"

#include "config/chainparams.h"
#include "chaincontrol/coins.h"
#include "config/consensus.h"

#include "rpc/client.h"
#include "rpc/protocol.h"

#include "utils/utilstrencodings.h"
#include "utils/net/events.h"
#include "utils/base58.h"
#include "utils/util.h"
#include "utils/utilmoneystr.h"

#include "sbtccore/clientversion.h"
#include "sbtccore/core_io.h"
#include "sbtccore/transaction/policy.h"

#include "wallet/keystore.h"
#include "wallet/rbf.h"
#include "transaction/transaction.h"
#include "script/script.h"
#include "script/sign.h"

#define  COMMAND_ARG_SEP '`'

static bool fCreateBlank = false;
static std::map<std::string, UniValue> registers;
static const int CONTINUE_EXECUTION = -1;

static void RegisterSetJson(const std::string &key, const std::string &rawJson)
{
    UniValue val;
    if (!val.read(rawJson))
    {
        std::string strErr = "Cannot parse JSON for key " + key;
        throw std::runtime_error(strErr);
    }

    registers[key] = val;
}

static void RegisterSet(const std::string &strInput)
{
    // separate NAME:VALUE in string
    size_t pos = strInput.find(':');
    if ((pos == std::string::npos) ||
        (pos == 0) ||
        (pos == (strInput.size() - 1)))
        throw std::runtime_error("Register input requires NAME:VALUE");

    std::string key = strInput.substr(0, pos);
    std::string valStr = strInput.substr(pos + 1, std::string::npos);

    RegisterSetJson(key, valStr);
}

static void RegisterLoad(const std::string &strInput)
{
    // separate NAME:FILENAME in string
    size_t pos = strInput.find(':');
    if ((pos == std::string::npos) ||
        (pos == 0) ||
        (pos == (strInput.size() - 1)))
        throw std::runtime_error("Register load requires NAME:FILENAME");

    std::string key = strInput.substr(0, pos);
    std::string filename = strInput.substr(pos + 1, std::string::npos);

    FILE *f = fopen(filename.c_str(), "r");
    if (!f)
    {
        std::string strErr = "Cannot open file " + filename;
        throw std::runtime_error(strErr);
    }

    // load file chunks into one big buffer
    std::string valStr;
    while ((!feof(f)) && (!ferror(f)))
    {
        char buf[4096];
        int bread = fread(buf, 1, sizeof(buf), f);
        if (bread <= 0)
            break;

        valStr.insert(valStr.size(), buf, bread);
    }

    int error = ferror(f);
    fclose(f);

    if (error)
    {
        std::string strErr = "Error reading file " + filename;
        throw std::runtime_error(strErr);
    }

    // evaluate as JSON buffer register
    RegisterSetJson(key, valStr);
}

static CAmount ExtractAndValidateValue(const std::string &strValue)
{
    CAmount value;
    if (!ParseMoney(strValue, value))
        throw std::runtime_error("invalid TX output value");
    return value;
}

static void MutateTxVersion(CMutableTransaction &tx, const std::string &cmdVal)
{
    int64_t newVersion = atoi64(cmdVal);
    if (newVersion < 1 || newVersion > CTransaction::MAX_STANDARD_VERSION)
        throw std::runtime_error("Invalid TX version requested");

    tx.nVersion = (int)newVersion;
}

static void MutateTxLocktime(CMutableTransaction &tx, const std::string &cmdVal)
{
    int64_t newLocktime = atoi64(cmdVal);
    if (newLocktime < 0LL || newLocktime > 0xffffffffLL)
        throw std::runtime_error("Invalid TX locktime requested");

    tx.nLockTime = (unsigned int)newLocktime;
}

static void MutateTxRBFOptIn(CMutableTransaction &tx, const std::string &strInIdx)
{
    // parse requested index
    int inIdx = atoi(strInIdx);
    if (inIdx < 0 || inIdx >= (int)tx.vin.size())
    {
        throw std::runtime_error("Invalid TX input index '" + strInIdx + "'");
    }

    // set the nSequence to MAX_INT - 2 (= RBF opt in flag)
    int cnt = 0;
    for (CTxIn &txin : tx.vin)
    {
        if (strInIdx == "" || cnt == inIdx)
        {
            if (txin.nSequence > MAX_BIP125_RBF_SEQUENCE)
            {
                txin.nSequence = MAX_BIP125_RBF_SEQUENCE;
            }
        }
        ++cnt;
    }
}

static void MutateTxAddInput(CMutableTransaction &tx, const std::string &strInput)
{
    std::vector<std::string> vStrInputParts;
    boost::split(vStrInputParts, strInput, boost::is_any_of(":"));

    // separate TXID:VOUT in string
    if (vStrInputParts.size() < 2)
        throw std::runtime_error("TX input missing separator");

    // extract and validate TXID
    std::string strTxid = vStrInputParts[0];
    if ((strTxid.size() != 64) || !IsHex(strTxid))
        throw std::runtime_error("invalid TX input txid");
    uint256 txid(uint256S(strTxid));

    static const unsigned int minTxOutSz = 9;
    static const unsigned int maxVout = MAX_BLOCK_WEIGHT / (WITNESS_SCALE_FACTOR * minTxOutSz);

    // extract and validate vout
    std::string strVout = vStrInputParts[1];
    int vout = atoi(strVout);
    if ((vout < 0) || (vout > (int)maxVout))
        throw std::runtime_error("invalid TX input vout");

    // extract the optional sequence number
    uint32_t nSequenceIn = std::numeric_limits<unsigned int>::max();
    if (vStrInputParts.size() > 2)
        nSequenceIn = std::stoul(vStrInputParts[2]);

    // append to transaction input list
    CTxIn txin(txid, vout, CScript(), nSequenceIn);
    tx.vin.push_back(txin);
}

static void MutateTxAddOutAddr(CMutableTransaction &tx, const std::string &strInput)
{
    // Separate into VALUE:ADDRESS
    std::vector<std::string> vStrInputParts;
    boost::split(vStrInputParts, strInput, boost::is_any_of(":"));

    if (vStrInputParts.size() != 2)
        throw std::runtime_error("TX output missing or too many separators");

    // Extract and validate VALUE
    CAmount value = ExtractAndValidateValue(vStrInputParts[0]);

    // extract and validate ADDRESS
    std::string strAddr = vStrInputParts[1];
    CBitcoinAddress addr(strAddr);
    if (!addr.IsValid())
        throw std::runtime_error("invalid TX output address");
    // build standard output script via GetScriptForDestination()
    CScript scriptPubKey = GetScriptForDestination(addr.Get());

    // construct TxOut, append to transaction output list
    CTxOut txout(value, scriptPubKey);
    tx.vout.push_back(txout);
}

static void MutateTxAddOutPubKey(CMutableTransaction &tx, const std::string &strInput)
{
    // Separate into VALUE:PUBKEY[:FLAGS]
    std::vector<std::string> vStrInputParts;
    boost::split(vStrInputParts, strInput, boost::is_any_of(":"));

    if (vStrInputParts.size() < 2 || vStrInputParts.size() > 3)
        throw std::runtime_error("TX output missing or too many separators");

    // Extract and validate VALUE
    CAmount value = ExtractAndValidateValue(vStrInputParts[0]);

    // Extract and validate PUBKEY
    CPubKey pubkey(ParseHex(vStrInputParts[1]));
    if (!pubkey.IsFullyValid())
        throw std::runtime_error("invalid TX output pubkey");
    CScript scriptPubKey = GetScriptForRawPubKey(pubkey);

    // Extract and validate FLAGS
    bool bSegWit = false;
    bool bScriptHash = false;
    if (vStrInputParts.size() == 3)
    {
        std::string flags = vStrInputParts[2];
        bSegWit = (flags.find("W") != std::string::npos);
        bScriptHash = (flags.find("S") != std::string::npos);
    }

    if (bSegWit)
    {
        if (!pubkey.IsCompressed())
        {
            throw std::runtime_error("Uncompressed pubkeys are not useable for SegWit outputs");
        }
        // Call GetScriptForWitness() to build a P2WSH scriptPubKey
        scriptPubKey = GetScriptForWitness(scriptPubKey);
    }
    if (bScriptHash)
    {
        // Get the address for the redeem script, then call
        // GetScriptForDestination() to construct a P2SH scriptPubKey.
        CBitcoinAddress redeemScriptAddr(scriptPubKey);
        scriptPubKey = GetScriptForDestination(redeemScriptAddr.Get());
    }

    // construct TxOut, append to transaction output list
    CTxOut txout(value, scriptPubKey);
    tx.vout.push_back(txout);
}

static void MutateTxAddOutMultiSig(CMutableTransaction &tx, const std::string &strInput)
{
    // Separate into VALUE:REQUIRED:NUMKEYS:PUBKEY1:PUBKEY2:....[:FLAGS]
    std::vector<std::string> vStrInputParts;
    boost::split(vStrInputParts, strInput, boost::is_any_of(":"));

    // Check that there are enough parameters
    if (vStrInputParts.size() < 3)
        throw std::runtime_error("Not enough multisig parameters");

    // Extract and validate VALUE
    CAmount value = ExtractAndValidateValue(vStrInputParts[0]);

    // Extract REQUIRED
    uint32_t required = stoul(vStrInputParts[1]);

    // Extract NUMKEYS
    uint32_t numkeys = stoul(vStrInputParts[2]);

    // Validate there are the correct number of pubkeys
    if (vStrInputParts.size() < numkeys + 3)
        throw std::runtime_error("incorrect number of multisig pubkeys");

    if (required < 1 || required > 20 || numkeys < 1 || numkeys > 20 || numkeys < required)
        throw std::runtime_error("multisig parameter mismatch. Required " \
 + std::to_string(required) + " of " + std::to_string(numkeys) + "signatures.");

    // extract and validate PUBKEYs
    std::vector<CPubKey> pubkeys;
    for (int pos = 1; pos <= int(numkeys); pos++)
    {
        CPubKey pubkey(ParseHex(vStrInputParts[pos + 2]));
        if (!pubkey.IsFullyValid())
            throw std::runtime_error("invalid TX output pubkey");
        pubkeys.push_back(pubkey);
    }

    // Extract FLAGS
    bool bSegWit = false;
    bool bScriptHash = false;
    if (vStrInputParts.size() == numkeys + 4)
    {
        std::string flags = vStrInputParts.back();
        bSegWit = (flags.find("W") != std::string::npos);
        bScriptHash = (flags.find("S") != std::string::npos);
    } else if (vStrInputParts.size() > numkeys + 4)
    {
        // Validate that there were no more parameters passed
        throw std::runtime_error("Too many parameters");
    }

    CScript scriptPubKey = GetScriptForMultisig(required, pubkeys);

    if (bSegWit)
    {
        for (CPubKey &pubkey : pubkeys)
        {
            if (!pubkey.IsCompressed())
            {
                throw std::runtime_error("Uncompressed pubkeys are not useable for SegWit outputs");
            }
        }
        // Call GetScriptForWitness() to build a P2WSH scriptPubKey
        scriptPubKey = GetScriptForWitness(scriptPubKey);
    }
    if (bScriptHash)
    {
        // Get the address for the redeem script, then call
        // GetScriptForDestination() to construct a P2SH scriptPubKey.
        CBitcoinAddress addr(scriptPubKey);
        scriptPubKey = GetScriptForDestination(addr.Get());
    }

    // construct TxOut, append to transaction output list
    CTxOut txout(value, scriptPubKey);
    tx.vout.push_back(txout);
}

static void MutateTxAddOutData(CMutableTransaction &tx, const std::string &strInput)
{
    CAmount value = 0;

    // separate [VALUE:]DATA in string
    size_t pos = strInput.find(':');

    if (pos == 0)
        throw std::runtime_error("TX output value not specified");

    if (pos != std::string::npos)
    {
        // Extract and validate VALUE
        value = ExtractAndValidateValue(strInput.substr(0, pos));
    }

    // extract and validate DATA
    std::string strData = strInput.substr(pos + 1, std::string::npos);

    if (!IsHex(strData))
        throw std::runtime_error("invalid TX output data");

    std::vector<unsigned char> data = ParseHex(strData);

    CTxOut txout(value, CScript() << OP_RETURN << data);
    tx.vout.push_back(txout);
}

static void MutateTxAddOutScript(CMutableTransaction &tx, const std::string &strInput)
{
    // separate VALUE:SCRIPT[:FLAGS]
    std::vector<std::string> vStrInputParts;
    boost::split(vStrInputParts, strInput, boost::is_any_of(":"));
    if (vStrInputParts.size() < 2)
        throw std::runtime_error("TX output missing separator");

    // Extract and validate VALUE
    CAmount value = ExtractAndValidateValue(vStrInputParts[0]);

    // extract and validate script
    std::string strScript = vStrInputParts[1];
    CScript scriptPubKey = ParseScript(strScript);

    // Extract FLAGS
    bool bSegWit = false;
    bool bScriptHash = false;
    if (vStrInputParts.size() == 3)
    {
        std::string flags = vStrInputParts.back();
        bSegWit = (flags.find("W") != std::string::npos);
        bScriptHash = (flags.find("S") != std::string::npos);
    }

    if (bSegWit)
    {
        scriptPubKey = GetScriptForWitness(scriptPubKey);
    }
    if (bScriptHash)
    {
        CBitcoinAddress addr(scriptPubKey);
        scriptPubKey = GetScriptForDestination(addr.Get());
    }

    // construct TxOut, append to transaction output list
    CTxOut txout(value, scriptPubKey);
    tx.vout.push_back(txout);
}

static void MutateTxDelInput(CMutableTransaction &tx, const std::string &strInIdx)
{
    // parse requested deletion index
    int inIdx = atoi(strInIdx);
    if (inIdx < 0 || inIdx >= (int)tx.vin.size())
    {
        std::string strErr = "Invalid TX input index '" + strInIdx + "'";
        throw std::runtime_error(strErr.c_str());
    }

    // delete input from transaction
    tx.vin.erase(tx.vin.begin() + inIdx);
}

static void MutateTxDelOutput(CMutableTransaction &tx, const std::string &strOutIdx)
{
    // parse requested deletion index
    int outIdx = atoi(strOutIdx);
    if (outIdx < 0 || outIdx >= (int)tx.vout.size())
    {
        std::string strErr = "Invalid TX output index '" + strOutIdx + "'";
        throw std::runtime_error(strErr.c_str());
    }

    // delete output from transaction
    tx.vout.erase(tx.vout.begin() + outIdx);
}

static const unsigned int N_SIGHASH_OPTS = 12;
static const struct
{
    const char *flagStr;
    int flags;
} sighashOptions[N_SIGHASH_OPTS] = {
        {"ALL",                           SIGHASH_ALL},
        {"NONE",                          SIGHASH_NONE},
        {"SINGLE",                        SIGHASH_SINGLE},
        {"ALL|ANYONECANPAY",              SIGHASH_ALL | SIGHASH_ANYONECANPAY},
        {"NONE|ANYONECANPAY",             SIGHASH_NONE | SIGHASH_ANYONECANPAY},
        {"SINGLE|ANYONECANPAY",           SIGHASH_SINGLE | SIGHASH_ANYONECANPAY},
        {"ALL|SBTC_FORK",                 SIGHASH_ALL | SIGHASH_SBTC_FORK},
        {"NONE|SBTC_FORK",                SIGHASH_NONE | SIGHASH_SBTC_FORK},
        {"SINGLE|SBTC_FORK",              SIGHASH_SINGLE | SIGHASH_SBTC_FORK},
        {"ALL|SBTC_FORK|ANYONECANPAY",    SIGHASH_ALL | SIGHASH_SBTC_FORK | SIGHASH_ANYONECANPAY},
        {"NONE|SBTC_FORK|ANYONECANPAY",   SIGHASH_NONE | SIGHASH_SBTC_FORK | SIGHASH_ANYONECANPAY},
        {"SINGLE|SBTC_FORK|ANYONECANPAY", SIGHASH_SINGLE | SIGHASH_SBTC_FORK | SIGHASH_ANYONECANPAY},
};

static bool findSighashFlags(int &flags, const std::string &flagStr)
{
    flags = 0;

    for (unsigned int i = 0; i < N_SIGHASH_OPTS; i++)
    {
        if (flagStr == sighashOptions[i].flagStr)
        {
            flags = sighashOptions[i].flags;
            return true;
        }
    }

    return false;
}

static CAmount AmountFromValue(const UniValue &value)
{
    if (!value.isNum() && !value.isStr())
        throw std::runtime_error("Amount is not a number or string");
    CAmount amount;
    if (!ParseFixedPoint(value.getValStr(), 8, &amount))
        throw std::runtime_error("Invalid amount");
    if (!MoneyRange(amount))
        throw std::runtime_error("Amount out of range");
    return amount;
}

static void MutateTxSign(CMutableTransaction &tx, const std::string &flagStr)
{
    int nHashType = SIGHASH_ALL | SIGHASH_SBTC_FORK;

    if (flagStr.size() > 0)
        if (!findSighashFlags(nHashType, flagStr))
            throw std::runtime_error("unknown sighash flag/sign option");

    std::vector<CTransaction> txVariants;
    txVariants.push_back(tx);

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the raw tx:
    CMutableTransaction mergedTx(txVariants[0]);
    bool fComplete = true;
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);

    if (!registers.count("privatekeys"))
        throw std::runtime_error("privatekeys register variable must be set.");
    CBasicKeyStore tempKeystore;
    UniValue keysObj = registers["privatekeys"];

    for (unsigned int kidx = 0; kidx < keysObj.size(); kidx++)
    {
        if (!keysObj[kidx].isStr())
            throw std::runtime_error("privatekey not a std::string");
        CBitcoinSecret vchSecret;
        bool fGood = vchSecret.SetString(keysObj[kidx].getValStr());
        if (!fGood)
            throw std::runtime_error("privatekey not valid");

        CKey key = vchSecret.GetKey();
        tempKeystore.AddKey(key);
    }

    // Add previous txouts given in the RPC call:
    if (!registers.count("prevtxs"))
        throw std::runtime_error("prevtxs register variable must be set.");
    UniValue prevtxsObj = registers["prevtxs"];
    {
        for (unsigned int previdx = 0; previdx < prevtxsObj.size(); previdx++)
        {
            UniValue prevOut = prevtxsObj[previdx];
            if (!prevOut.isObject())
                throw std::runtime_error("expected prevtxs internal object");

            std::map<std::string, UniValue::VType> types = {
                    {"txid",         UniValue::VSTR},
                    {"vout",         UniValue::VNUM},
                    {"scriptPubKey", UniValue::VSTR},
            };
            if (!prevOut.checkObject(types))
                throw std::runtime_error("prevtxs internal object typecheck fail");

            uint256 txid = ParseHashUV(prevOut["txid"], "txid");

            int nOut = atoi(prevOut["vout"].getValStr());
            if (nOut < 0)
                throw std::runtime_error("vout must be positive");

            COutPoint out(txid, nOut);
            std::vector<unsigned char> pkData(ParseHexUV(prevOut["scriptPubKey"], "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                const Coin &coin = view.AccessCoin(out);
                if (!coin.IsSpent() && coin.out.scriptPubKey != scriptPubKey)
                {
                    std::string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coin.out.scriptPubKey) + "\nvs:\n" +
                          ScriptToAsmStr(scriptPubKey);
                    throw std::runtime_error(err);
                }
                Coin newcoin;
                newcoin.out.scriptPubKey = scriptPubKey;
                newcoin.out.nValue = 0;
                if (prevOut.exists("amount"))
                {
                    newcoin.out.nValue = AmountFromValue(prevOut["amount"]);
                }
                newcoin.nHeight = 1;
                view.AddCoin(out, std::move(newcoin), true);
            }

            // if redeemScript given and private keys given,
            // add redeemScript to the tempKeystore so it can be signed:
            if ((scriptPubKey.IsPayToScriptHash() || scriptPubKey.IsPayToWitnessScriptHash()) &&
                prevOut.exists("redeemScript"))
            {
                UniValue v = prevOut["redeemScript"];
                std::vector<unsigned char> rsData(ParseHexUV(v, "redeemScript"));
                CScript redeemScript(rsData.begin(), rsData.end());
                tempKeystore.AddCScript(redeemScript);
            }
        }
    }

    const CKeyStore &keystore = tempKeystore;

    bool fHashSingle = ((nHashType & ~(SIGHASH_ANYONECANPAY | SIGHASH_SBTC_FORK)) == SIGHASH_SINGLE);

    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++)
    {
        CTxIn &txin = mergedTx.vin[i];
        const Coin &coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent())
        {
            fComplete = false;
            continue;
        }
        const CScript &prevPubKey = coin.out.scriptPubKey;
        const CAmount &amount = coin.out.nValue;

        SignatureData sigdata;
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mergedTx.vout.size()))
            ProduceSignature(MutableTransactionSignatureCreator(&keystore, &mergedTx, i, amount, nHashType), prevPubKey,
                             sigdata);

        // ... and merge in other signatures:
        for (const CTransaction &txv : txVariants)
            sigdata = CombineSignatures(prevPubKey, MutableTransactionSignatureChecker(&mergedTx, i, amount), sigdata,
                                        DataFromTransaction(txv, i));
        UpdateTransaction(mergedTx, i, sigdata);

        if (!VerifyScript(txin.scriptSig, prevPubKey, &txin.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS,
                          MutableTransactionSignatureChecker(&mergedTx, i, amount)))
            fComplete = false;
    }

    if (fComplete)
    {
        // do nothing... for now
        // perhaps store this for later optional JSON output
    }

    tx = mergedTx;
}

class Secp256k1Init
{
    ECCVerifyHandle globalVerifyHandle;

public:
    Secp256k1Init()
    {
        ECC_Start();
    }

    ~Secp256k1Init()
    {
        ECC_Stop();
    }
};

static void MutateTx(CMutableTransaction &tx, const std::string &command,
                     const std::string &commandVal)
{
    std::unique_ptr<Secp256k1Init> ecc;

    if (command == "nversion")
        MutateTxVersion(tx, commandVal);
    else if (command == "locktime")
        MutateTxLocktime(tx, commandVal);
    else if (command == "replaceable")
    {
        MutateTxRBFOptIn(tx, commandVal);
    } else if (command == "delin")
        MutateTxDelInput(tx, commandVal);
    else if (command == "in")
        MutateTxAddInput(tx, commandVal);

    else if (command == "delout")
        MutateTxDelOutput(tx, commandVal);
    else if (command == "outaddr")
        MutateTxAddOutAddr(tx, commandVal);
    else if (command == "outpubkey")
    {
        if (!ecc)
        {
            ecc.reset(new Secp256k1Init());
        }
        MutateTxAddOutPubKey(tx, commandVal);
    } else if (command == "outmultisig")
    {
        if (!ecc)
        {
            ecc.reset(new Secp256k1Init());
        }
        MutateTxAddOutMultiSig(tx, commandVal);
    } else if (command == "outscript")
        MutateTxAddOutScript(tx, commandVal);
    else if (command == "outdata")
        MutateTxAddOutData(tx, commandVal);

    else if (command == "sign")
    {
        if (!ecc)
        {
            ecc.reset(new Secp256k1Init());
        }
        MutateTxSign(tx, commandVal);
    } else if (command == "load")
        RegisterLoad(commandVal);

    else if (command == "set")
        RegisterSet(commandVal);

    else
        throw std::runtime_error("unknown command");
}

static void OutputTxJSON(const CTransaction &tx)
{
    UniValue entry(UniValue::VOBJ);
    TxToUniv(tx, uint256(), entry);

    std::string jsonOutput = entry.write(4);
    fprintf(stdout, "%s\n", jsonOutput.c_str());
}

static void OutputTxHash(const CTransaction &tx)
{
    std::string strHexHash = tx.GetHash().GetHex(); // the hex-encoded transaction hash (aka the transaction id)

    fprintf(stdout, "%s\n", strHexHash.c_str());
}

static void OutputTxHex(const CTransaction &tx)
{
    std::string strHex = EncodeHexTx(tx);

    fprintf(stdout, "%s\n", strHex.c_str());
}

static void OutputTx(const CTransaction &tx)
{
    if (Args().GetArg<bool>("-json", false))
        OutputTxJSON(tx);
    else if (Args().GetArg<bool>("-txid", false))
        OutputTxHash(tx);
    else
        OutputTxHex(tx);
}

std::string readStdin()
{
    char buf[4096];
    std::string ret;

    while (!feof(stdin))
    {
        size_t bread = fread(buf, 1, sizeof(buf), stdin);
        ret.append(buf, bread);
        if (bread < sizeof(buf))
            break;
    }

    if (ferror(stdin))
        throw std::runtime_error("error reading stdin");

    boost::algorithm::trim_right(ret);

    return ret;
}

void CApp::InitOptionMap()
{
    std::map<string, vector<option_item>> optionMap;

    vector<option_item> item = {
            {"help,h", "Print this message and exit."},
            {"create", "Create new, empty TX."},
            {"json",   "Select JSON output"},
            {"txid",   "Output only the hex-encoded transaction id of the resultant transaction."}
    };
    optionMap.emplace("Options:", item);

    item = {
            {"testnet", "Use the test chain"},
            {"regtest", "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
                                                      "This is intended for regression testing tools and app_bpo development."}

    };
    optionMap.emplace("Chain selection options:", item);

    item = {
            {"delin=N",                                                         "Delete input N from TX"},
            {"delout=N",                                                        "Delete output N from TX"},
            {"in=TXID:VOUT(:SEQUENCE_NUMBER)",                                  "Add input to TX"},
            {"locktime=N",                                                      "Set TX lock time to N"},
            {"nversion=N",                                                      "Set TX version to N"},
            {"replaceable(=N)",                                                 "Set RBF opt-in sequence number for input N (if not provided, opt-in all available inputs)"},
            {"outaddr=VALUE:ADDRESS",                                           "Add address-based output to TX"},
            {"outpubkey=VALUE:PUBKEY[:FLAGS]",                                  _("Add pay-to-pubkey output to TX") +
                                                                                ". " +
                                                                                _("Optionally add the \"W\" flag to produce a pay-to-witness-pubkey-hash output") +
                                                                                ". " +
                                                                                _("Optionally add the \"S\" flag to wrap the output in a pay-to-script-hash.")},
            {"outdata=[VALUE:]DATA",                                            "Add data-based output to TX"},
            {"outscript=VALUE:SCRIPT[:FLAGS]",                                  _("Add raw script output to TX") +
                                                                                ". " +
                                                                                _("Optionally add the \"W\" flag to produce a pay-to-witness-script-hash output") +
                                                                                ". " +
                                                                                _("Optionally add the \"S\" flag to wrap the output in a pay-to-script-hash.")},
            {"outmultisig=VALUE:REQUIRED:PUBKEYS:PUBKEY1:PUBKEY2:....[:FLAGS]", _(
                    "Add Pay To n-of-m Multi-sig output to TX. n = REQUIRED, m = PUBKEYS") + ". " +
                                                                                _("Optionally add the \"W\" flag to produce a pay-to-witness-script-hash output") +
                                                                                ". " +
                                                                                _("Optionally add the \"S\" flag to wrap the output in a pay-to-script-hash.")},
            {"sign=SIGHASH-FLAGS",                                              _(
                    "Add zero or more signatures to transaction") + ". " +
                                                                                _("This command requires JSON registers:") +
                                                                                _("prevtxs=JSON object") + ", " +
                                                                                _("privatekeys=JSON object") + ". " +
                                                                                _("See signrawtransaction docs for format of sighash flags, JSON objects.")},
    };
    optionMap.emplace("Commands:", item);

    item = {
            {"load=NAME:FILENAME",   _("Load JSON file FILENAME into register NAME")},
            {"set=NAME:JSON-STRING", _("Set register NAME to given JSON-STRING")}
    };
    optionMap.emplace("Register Commands:", item);

    item = {
            {"hexsbtctx",        bpo::value<string>(), "Internal hex-encoded sbtc transaction"},
            {"commandargs",      bpo::value<string>(), "Internal Command arguments"}
    };
    optionMap.emplace("internal options(unavailable for command line):", item);

    std::string strHead =
            strprintf(_("%s sbtc-tx utility version"), _(PACKAGE_NAME)) + " " + FormatFullVersion() + "\n\n" +
            _("Usage:") + "\n" +
            "  bitcoin-tx [options] <hex-tx> [commands]  " + _("Update hex-encoded bitcoin transaction") + "\n" +
            "  bitcoin-tx [options] -create [commands]   " + _("Create hex-encoded bitcoin transaction") + "\n" +
            "\n";

    pArgs->SetOptionName(strHead);
    pArgs->SetOptionTable(optionMap);
}

void CApp::RelayoutArgs(int& argc, char**& argv)
{
    static std::list<std::string> _argx;
    static std::vector<const char*> _argv;

    _argv.emplace_back(argv[0]); // assert(argc >= 1)

    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++)
    {
        if (strlen(argv[i]) > 2 && argv[i][1] != '-')
        {
            _argx.emplace_back('-' + std::string(argv[i]));
            _argv.emplace_back(_argx.back().c_str());
        }
        else
        {
            _argv.emplace_back(argv[i]);
        }
    }

    for (auto opt : _argv)
    {
        if (strncmp(opt, "--create", 8) == 0)
        {
            fCreateBlank = true;
            break;
        }
    }

    if (i < argc)
    {
        if (!fCreateBlank)
        {
            _argx.emplace_back(std::string("--hexsbtctx=") + argv[i++]);
            _argv.emplace_back(_argx.back().c_str());
        }
    }

    if (i < argc)
    {
        std::string commandargs(argv[i++]);
        for (; i < argc; i++)
        {
            commandargs += COMMAND_ARG_SEP;
            commandargs += argv[i];
        }
        _argx.emplace_back(std::string("--commandargs=") + commandargs);
        _argv.emplace_back(_argx.back().c_str());
    }

    argc = (int)_argv.size();
    argv = (char**)&_argv[0];
}

bool CApp::Initialize(int argc, char **argv)
{
    SetupEnvironment();

    InitOptionMap();

    if (!pArgs->Init(argc, argv))
    {
        return false;
    }

    if (pArgs->IsArgSet("help") || pArgs->IsArgSet("usage"))
    {
        std::cout << pArgs->GetHelpMessage();
        return false;
    }

    try
    {
        pChainParams = CreateChainParams(ChainNameFromCommandLine());
    } catch(const std::exception& e)
    {
        fprintf(stderr, "Error: %s.\n", e.what());
        return false;
    }
    

    //InitializeLogging(pArgs->GetDataDir(false));
    return true;
}

bool CApp::Run()
{
    bool fUpdateTx = pArgs->IsArgSet("-hexsbtctx");

    if (fUpdateTx == fCreateBlank)
    {
        fprintf(stderr, "Error: Invalid command line arguments.\n");
        return false;
    }

    int nRet = 0;
    std::string strPrint;

    try
    {
        CMutableTransaction tx;
        if (fUpdateTx)
        {
            // param: hex-encoded bitcoin transaction
            std::string strHexTx = pArgs->GetArg<std::string>("-hexsbtctx", "");

            //if (strHexTx == "-")                 // "-" implies standard input
            //    strHexTx = readStdin();

            if (!DecodeHexTx(tx, strHexTx, true))
                throw std::runtime_error("invalid transaction encoding");
        }

        std::string strArgs = pArgs->GetArg<std::string>("-commandargs", "");
        std::vector<std::string> args = SplitString(strArgs, COMMAND_ARG_SEP);

        for (const auto& arg : args)
        {
            std::string key, value;
            size_t eqpos = arg.find('=');
            if (eqpos == std::string::npos)
                key = arg;
            else
            {
                key = arg.substr(0, eqpos);
                value = arg.substr(eqpos + 1);
            }

            MutateTx(tx, key, value);
        }

        OutputTx(tx);
    }
    catch (const boost::thread_interrupted &)
    {
        strPrint = "thread interrupted!";
        nRet = EXIT_FAILURE;
    }
    catch (const std::exception &e)
    {
        strPrint = std::string("error: ") + e.what();
        nRet = EXIT_FAILURE;
    }
    catch (...)
    {
        PrintExceptionContinue(nullptr, "CommandLineRawTx()");
        nRet = EXIT_FAILURE;
    }

    if (!strPrint.empty())
    {
        fprintf((nRet == EXIT_SUCCESS ? stdout : stderr), "%s\n", strPrint.c_str());
    }

    return nRet == EXIT_SUCCESS;
}

