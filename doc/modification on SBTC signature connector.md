* The trading format of SBTC is more or less the same. Main difference are two points:<br>
    * FORKID is added to the hashtype of the signature<br>
    * If there is FORKID in the signature hashtype, “sbtc” will be added to GetHash()<br>
    (Note: before the "sbtc", add the length value, as follows: **04**73627463）**Attention:04 is must be need**.<br>

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
#Example:
{
	"txid": "5b11df4832b84e95a7b03b2e32ca0bd5c8051f8901d8fed997ecf16dda83df9c",
	"hash": "5b11df4832b84e95a7b03b2e32ca0bd5c8051f8901d8fed997ecf16dda83df9c",
	"version": 2,
	"size": 191,
	"vsize": 191,
	"locktime": 1101,
	"vin": [{
		"txid": "22c384d4c1070bc983af026b2386956b3d57d175ad41ef29c08a66926cd3e283",
		"vout": 0,
		"scriptSig": {
			"asm": "3044022074765c87ba0814d13893be7ed5dfbdf6253de25ec610fa0bd3c5bd0046f3fe1002207c7cdf55a3158fa4bb76442533d129391d84f30df098817b8772bd4e56f9f4be[ALL|SBTC_FORK]",
			"hex": "473044022074765c87ba0814d13893be7ed5dfbdf6253de25ec610fa0bd3c5bd0046f3fe1002207c7cdf55a3158fa4bb76442533d129391d84f30df098817b8772bd4e56f9f4be41"
		},
		"sequence": 4294967294
	}],
	"vout": [{
			"value": 12.00000000,
			"n": 0,
			"scriptPubKey": {
				"asm": "OP_DUP OP_HASH160 8f3ebd63167cfcda786a77ce330bff37612a23b9 OP_EQUALVERIFY OP_CHECKSIG",
				"hex": "76a9148f3ebd63167cfcda786a77ce330bff37612a23b988ac",
				"reqSigs": 1,
				"type": "pubkeyhash",
				"addresses": [
					"mtaN5itWgbNrjBzKXFHwGNbcEHL4RKR2N3"
				]
			}
		},
		{
			"value": 0.49996160,
			"n": 1,
			"scriptPubKey": {
				"asm": "OP_DUP OP_HASH160 9e6edbad276a8f2fa3ea3621e27c6a236d563e65 OP_EQUALVERIFY OP_CHECKSIG",
				"hex": "76a9149e6edbad276a8f2fa3ea3621e27c6a236d563e6588ac",
				"reqSigs": 1,
				"type": "pubkeyhash",
				"addresses": [
					"muxfrvJUk58or2X28kALqT4jpzQR1W2UVx"
				]
			}
		}
	],
	"hex": "020000000183e2d36c92668ac029ef41ad75d1573d6b9586236b02af83c90b07c1d484c3220000000048473044022074765c87ba0814d13893be7ed5dfbdf6253de25ec610fa0bd3c5bd0046f3fe1002207c7cdf55a3158fa4bb76442533d129391d84f30df098817b8772bd4e56f9f4be41feffffff02008c8647000000001976a9148f3ebd63167cfcda786a77ce330bff37612a23b988ac80e1fa02000000001976a9149e6edbad276a8f2fa3ea3621e27c6a236d563e6588ac4d040000"
}
```
#The source data used to calculate the signature HASH.
020000000183e2d36c92668ac029ef41ad75d1573d6b9586236b02af83c90b07c1d484c32200000000232102943696352fe793e440db5bb74c3333a71389630975d2f3ba80ea372b7c34bc2dacfeffffff02008c8647000000001976a9148f3ebd63167cfcda786a77ce330bff37612a23b988ac80e1fa02000000001976a9149e6edbad276a8f2fa3ea3621e27c6a236d563e6588ac4d040000410000000473627463

#The HASH value used to sign.<br>
674c5c78ebed24ffda49f7b4d8f30b441e7ea430a4780237b74f0ce4f815e07a

