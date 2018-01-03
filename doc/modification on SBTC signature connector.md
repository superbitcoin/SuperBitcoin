* The trading format of SBTC is more or less the same. Main difference are two points:<br>
    * FORKID is added to the hashtype of the signature<br>
    * If there is FORKID in the signature hashtype, “sbtc” will be added to GetHash()<br>
    (Note: before the "sbtc", add the length value, as follows: 0473627463）.<br>

* In doe:<br>
&ensp;&ensp;1.Custimized fork signature type<br>
    
``` c++
/** Signature hash types/flags */
enum
{
    SIGHASH_ALL = 1,
    SIGHASH_NONE = 2,
    SIGHASH_SINGLE = 3,
    SIGHASH_SBTC_FORK = 0x40,
    SIGHASH_ANYONECANPAY = 0x80,
};
```

``` c++
static const struct {
    const char *flagStr;
    int flags;
} sighashOptions[N_SIGHASH_OPTS] = {
        {"ALL", SIGHASH_ALL},
        {"NONE", SIGHASH_NONE},
        {"SINGLE", SIGHASH_SINGLE},
        {"ALL|ANYONECANPAY", SIGHASH_ALL|SIGHASH_ANYONECANPAY},
        {"NONE|ANYONECANPAY", SIGHASH_NONE|SIGHASH_ANYONECANPAY},
        {"SINGLE|ANYONECANPAY", SIGHASH_SINGLE|SIGHASH_ANYONECANPAY},
        {"ALL|SBTC_FORK", SIGHASH_ALL | SIGHASH_SBTC_FORK},
        {"NONE|SBTC_FORK", SIGHASH_NONE | SIGHASH_SBTC_FORK},
        {"SINGLE|SBTC_FORK", SIGHASH_SINGLE | SIGHASH_SBTC_FORK},
        {"ALL|SBTC_FORK|ANYONECANPAY", SIGHASH_ALL | SIGHASH_SBTC_FORK | SIGHASH_ANYONECANPAY},
        {"NONE|SBTC_FORK|ANYONECANPAY", SIGHASH_NONE | SIGHASH_SBTC_FORK | SIGHASH_ANYONECANPAY},
        {"SINGLE|SBTC_FORK|ANYONECANPAY", SIGHASH_SINGLE | SIGHASH_SBTC_FORK | SIGHASH_ANYONECANPAY},
};
```
&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;2.Add FORKID to the code of the signature part<br>
``` c++
uint256 SignatureHash(const CScript& scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType, const CAmount& amount, SigVersion sigversion, const PrecomputedTransactionData* cache){
......
    ss << hashOutputs;
    // Locktime
    ss << txTo.nLockTime;
    // Sighash type
    ss << nHashType;
    if (nHashType & SIGHASH_SBTC_FORK) {
        ss << std::string("sbtc");//attention : the adding data is 0473627463 
    }
    return ss.GetHash();
......
```
In Trading
``` json
{
  "txid": "148adc53629288da9f454082de274f11a529226ea3e717bb7d32edcd1b530ebc",
  "hash": "148adc53629288da9f454082de274f11a529226ea3e717bb7d32edcd1b530ebc",
  "version": 2,
  "size": 266,
  "vsize": 266,
  "locktime": 0,
  "vin": [
    {
      "txid": "592dea637e1fcf7979f05d64fcde8160e5e155a070a3169c52a5584ba3435d8b",
      "vout": 0,
      "scriptSig": {
        "asm": "0 3045022100999b81caed114a3ce216700e21f8340c9c0b2a6652f7c16dcd2a7776852209ad02207f529c43d64b5e6976c7a80bfc5b8d8dc2c3b8a51404cd37c08e8026863f4f30[ALL|SBTC_FORK] 52210367d84d80ed1a6df7dd61ec34cee1c27362c901b1e0d5775bf719cd68015d08f1210349fd4db36c27ea63f61064a7fd9bc99a10ddd1e633b1892d71279247276f2cd42103a3e3cdb4bc755c0a5c65162051bfee427d2258806668665f9de3915079a7159253ae",
        "hex": "00483045022100999b81caed114a3ce216700e21f8340c9c0b2a6652f7c16dcd2a7776852209ad02207f529c43d64b5e6976c7a80bfc5b8d8dc2c3b8a51404cd37c08e8026863f4f30414c6952210367d84d80ed1a6df7dd61ec34cee1c27362c901b1e0d5775bf719cd68015d08f1210349fd4db36c27ea63f61064a7fd9bc99a10ddd1e633b1892d71279247276f2cd42103a3e3cdb4bc755c0a5c65162051bfee427d2258806668665f9de3915079a7159253ae"
      },
      "sequence": 4294967295
    }
  ],
  "vout": [
    {
      "value": 1.24800000,
      "n": 0,
      "scriptPubKey": {
        "asm": "OP_DUP OP_HASH160 2072170412da7fe3c4f831688298be958b26abc6 OP_EQUALVERIFY OP_CHECKSIG",
        "hex": "76a9142072170412da7fe3c4f831688298be958b26abc688ac",
        "reqSigs": 1,
        "type": "pubkeyhash",
        "addresses": [
          "miUWbJFVcxXouXvLHqkFUG522P8r7ZBEu3"
        ]
      }
    }
  ]
}
```
