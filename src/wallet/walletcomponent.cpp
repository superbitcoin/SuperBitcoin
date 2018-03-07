/*************************************
 * File name:       walletcomponent.cpp
 * Author:          Adolph
 * Date:            2018.2.1
 * Description:     implement the wallet component common interface
 *
 * History:         Record the edit history
 ************************************/
#include "walletcomponent.h"
#include "utils/util.h"
#include "rpcwallet.h"
#include "server.h"
#include "scheduler.h"
#include "base.hpp"

CWalletComponent::CWalletComponent()
{

}

CWalletComponent::~CWalletComponent()
{
    for (CWalletRef pWallet : vpWallets)
    {
        pWallet->Flush(true);
    }

    for (CWalletRef pWallet : vpWallets)
    {
        delete pWallet;
    }
}

bool CWalletComponent::ComponentInitialize()
{
    RegisterWalletRPCCommands(tableRPC);

    return CWallet::ParameterInteraction() && CWallet::Verify();
}

bool CWalletComponent::ComponentStartup()
{
    if (gArgs.GetArg<bool>("-disablewallet", DEFAULT_DISABLE_WALLET))
    {
        mlog_notice("Wallet disabled!");
        return true;
    }

    vector<string> walletFiles = CWallet::GetWalletFiles();
    for (const auto &walletFile : walletFiles)
    {
        auto pwallet = CWallet::CreateWalletFromFile(walletFile);
        if (!pwallet)
        {
            return false;
        }
        vpWallets.push_back(pwallet);
    }

    for (CWalletRef pwallet : vpWallets)
    {
        // Add wallet transactions that aren't already in a block to mempool
        // Do this here as mempool requires genesis block to be loaded
        pwallet->ReacceptWalletTransactions();
    }

    if (!vpWallets.empty())
    {
        app().GetScheduler().scheduleEvery(std::bind(MaybeCompactWalletDB, vpWallets), 500);
    }

    return true;
}

bool CWalletComponent::ComponentShutdown()
{
    for (CWalletRef pWallet : vpWallets)
    {
        pWallet->Flush(true);
    }

//    for (CWalletRef pWallet : vpWallets)
//    {
//        delete pWallet;
//    }
//
//    vpWallets.clear();
    return true;
}

const char *CWalletComponent::whoru() const
{
    return "I am CWalletComponent\n";
}

int CWalletComponent::GetWalletCount() const
{
    return vpWallets.size();
}

CWallet* CWalletComponent::GetWallet(int index)
{
    if (index >= 0 && index < (int)vpWallets.size())
    {
        return vpWallets[index];
    }
    return nullptr;
}

log4cpp::Category &CWalletComponent::mlog = log4cpp::Category::getInstance(EMTOSTR(CID_WALLET));
log4cpp::Category &CWalletComponent::getLog()
{
    return mlog;
}
