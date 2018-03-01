#pragma once

#include "script.h"
#include "script_error.h"
#include "interpreter.h"
#include "wallet/amount.h"
#include "../transaction.h"

/**
 * Closure representing one script verification
 * Note that this stores references to the spending transaction
 */
class CScriptCheck
{
private:
    CScript scriptPubKey;
    CAmount amount;
    const CTransaction *ptxTo;
    unsigned int nIn;
    unsigned int nFlags;
    bool cacheStore;
    ScriptError error;
    PrecomputedTransactionData *txdata;

public:
    CScriptCheck() : amount(0), ptxTo(0), nIn(0), nFlags(0), cacheStore(false), error(SCRIPT_ERR_UNKNOWN_ERROR)
    {
    }

    CScriptCheck(const CScript &scriptPubKeyIn, const CAmount amountIn, const CTransaction &txToIn, unsigned int nInIn,
                 unsigned int nFlagsIn, bool cacheIn, PrecomputedTransactionData *txdataIn) :
            scriptPubKey(scriptPubKeyIn), amount(amountIn),
            ptxTo(&txToIn), nIn(nInIn), nFlags(nFlagsIn), cacheStore(cacheIn), error(SCRIPT_ERR_UNKNOWN_ERROR),
            txdata(txdataIn)
    {
    }

    bool operator()();

    void swap(CScriptCheck &check)
    {
        scriptPubKey.swap(check.scriptPubKey);
        std::swap(ptxTo, check.ptxTo);
        std::swap(amount, check.amount);
        std::swap(nIn, check.nIn);
        std::swap(nFlags, check.nFlags);
        std::swap(cacheStore, check.cacheStore);
        std::swap(error, check.error);
        std::swap(txdata, check.txdata);
    }

    ScriptError GetScriptError() const
    {
        return error;
    }
};

/** Initializes the script-execution cache */
void InitScriptExecutionCache();


