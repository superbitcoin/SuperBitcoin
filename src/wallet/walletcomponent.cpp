/*************************************
 * File name:       walletcomponent.cpp
 * Author:          Adolph
 * Date:            2018.2.1
 * Description:     implement the wallet component common interface
 *
 * History:         Record the edit history
 ************************************/

#include <walletcomponent.h>
#include "../interface/ibasecomponent.h"
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
    GET_BASE_INTERFACE(pBaseComponent);
    for (CWalletRef pwallet : vpwallets) {
        pwallet->postInitProcess(*(pBaseComponent->GetScheduler()));
    }

    return true;
}

bool CWalletComponent::ComponentShutdown()
{
    for (CWalletRef pWallet : vpwallets)
    {
        pWallet->Flush(true);
    }

    for (CWalletRef pWallet : vpwallets)
    {
        delete pWallet;
    }

    return true;
}
