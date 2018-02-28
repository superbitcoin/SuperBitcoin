///////////////////////////////////////////////////////////
//  utils.cpp
//  Created on:      28-2-2018 11:02:57
//  Original author: marco
///////////////////////////////////////////////////////////

#include "utils.h"
#include "framework/warnings.h"
#include "utils/util.h"
#include "utils/utilstrencodings.h"

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    fs::path path = GetDataDir();
    std::string tmp = path.string();
    uint64_t nFreeBytesAvailable = fs::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}

void AlertNotify(const std::string &strMessage)
{
    uiInterface.NotifyAlertChanged();

    const CArgsManager &cArgs = app().GetArgsManager();
    std::string strCmd = cArgs.GetArg<std::string>("-alertnotify", "");
    if (strCmd.empty())
        return;

    // Alert text should be plain ascii coming from a trusted source, but to
    // be safe we first strip anything not in safeChars, then add single quotes around
    // the whole string before passing it to the shell:
    std::string singleQuote("'");
    std::string safeStatus = SanitizeString(strMessage);
    safeStatus = singleQuote + safeStatus + singleQuote;
    boost::replace_all(strCmd, "%s", safeStatus);

    boost::thread t(runCommand, strCmd); // thread runs free
}

/** Abort with a message */
bool AbortNode(const std::string &strMessage, const std::string &userMessage)
{
    SetMiscWarning(strMessage);
    // TODU
//    mlog.error(strMessage);
//    string message = userMessage.empty() ? _("Error: A fatal internal error occurred, see debug.log for details")
//                                         : userMessage;
//    mlog.error(message);
//    uiInterface.ThreadSafeMessageBox(message, "", CClientUIInterface::MSG_ERROR);
    app().RequestShutdown();
    return false;
}

bool AbortNode(CValidationState &state, const std::string &strMessage, const std::string &userMessage)
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state)
{
    return strprintf("%s%s (code %i)",
                     state.GetRejectReason(),
                     state.GetDebugMessage().empty() ? "" : ", " + state.GetDebugMessage(),
                     state.GetRejectCode());
}

//! Guess how far we are in the verification process at the given block index
double GuessVerificationProgress(const ChainTxData &data, CBlockIndex *pindex)
{
    if (pindex == nullptr)
        return 0.0;

    int64_t nNow = time(nullptr);

    double fTxTotal;

    if (pindex->nChainTx <= data.nTxCount)
    {
        fTxTotal = data.nTxCount + (nNow - data.nTime) * data.dTxRate;
    } else
    {
        fTxTotal = pindex->nChainTx + (nNow - pindex->GetBlockTime()) * data.dTxRate;
    }

    return pindex->nChainTx / fTxTotal;
}
