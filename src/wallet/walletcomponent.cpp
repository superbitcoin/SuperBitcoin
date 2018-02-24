/*************************************
 * File name:       walletcomponent.cpp
 * Author:          Adolph
 * Date:            2018.2.1
 * Description:     implement the wallet component common interface
 *
 * History:         Record the edit history
 ************************************/

#include <walletcomponent.h>
#include <utils/util.h>
#include "base.hpp"

bool CWalletComponent::ComponentInitialize()
{
    RegisterWalletRPCCommands(tableRPC);

    if (!CWallet::ParameterInteraction())
    {
        return false;
    }
    if (!CWallet::Verify())
    {
        return false;
    }

    return true;
}

bool CWalletComponent::ComponentStartup()
{
    if (!CWallet::InitLoadWallet())
    {
        return false;
    }
    for (CWalletRef pwallet : vpWallets) {
        pwallet->postInitProcess(app().GetScheduler());
    }

    return true;
}

bool CWalletComponent::ComponentShutdown()
{
    for (CWalletRef pWallet : vpWallets)
    {
        pWallet->Flush(true);
    }

    for (CWalletRef pWallet : vpWallets)
    {
        delete pWallet;
    }

    vpWallets.clear();

    return true;
}

log4cpp::Category &CWalletComponent::mlog = log4cpp::Category::getInstance(EMTOSTR(CID_WALLET));
log4cpp::Category &CWalletComponent::getLog()
{
    return mlog;
}
