#include <log4cpp/Category.hh>

#include "base/base.hpp"
#include "scriptcheck.h"
#include "utils/util.h"

bool CScriptCheck::operator()()
{
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    const CScriptWitness *witness = &ptxTo->vin[nIn].scriptWitness;
    return VerifyScript(scriptSig, scriptPubKey, witness, nFlags,
                        CachingTransactionSignatureChecker(ptxTo, nIn, amount, cacheStore, *txdata), &error);
}

uint256 scriptExecutionCacheNonce(GetRandHash());
CuckooCache::cache<uint256, SignatureCacheHasher> scriptExecutionCache;
/** Initializes the script-execution cache */
void InitScriptExecutionCache(int64_t maxsigcachesize)
{
    // nMaxCacheSize is unsigned. If -maxsigcachesize is set to zero,
    // setup_bytes creates the minimum possible cache (2 elements).
    size_t nMaxCacheSize = std::min(std::max((int64_t)0, maxsigcachesize), MAX_MAX_SIG_CACHE_SIZE) * ((size_t) 1 << 20);
    size_t nElems = scriptExecutionCache.setup_bytes(nMaxCacheSize);
    SET_TEMP_LOG_CATEGORY(CID_TX_CORE);
    NLogFormat("Using %zu MiB out of %zu/2 requested for script execution cache, able to store %zu elements",
              (nElems*sizeof(uint256)) >>20, (nMaxCacheSize*2)>>20, nElems);
}
