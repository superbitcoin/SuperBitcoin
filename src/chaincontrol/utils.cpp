///////////////////////////////////////////////////////////
//  utils.cpp
//  Created on:      28-2-2018 11:02:57
//  Original author: marco
///////////////////////////////////////////////////////////

#include "utils.h"
#include "framework/warnings.h"
#include "utils/util.h"

/** Abort with a message */
bool AbortNode(const std::string &strMessage, const std::string &userMessage)
{
    SetMiscWarning(strMessage);
    LogPrintf("*** %s\n", strMessage);
    // TODO
//    uiInterface.ThreadSafeMessageBox(
//            userMessage.empty() ? _("Error: A fatal internal error occurred, see debug.log for details")
//                                : userMessage,
//            "", CClientUIInterface::MSG_ERROR);
    app().RequestShutdown();
    return false;
}


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

