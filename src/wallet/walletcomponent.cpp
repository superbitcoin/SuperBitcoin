#include "walletcomponent.h"
#include "utils/util.h"
#include "rpcwallet.h"
#include "server.h"
#include "scheduler.h"

REDIRECT_SBTC_LOGGER(CID_WALLET);

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
    ILogStream() << "Wallet component initialize";

    RegisterWalletRPCCommands(tableRPC);

    return CWallet::ParameterInteraction() && CWallet::Verify();
}

bool CWalletComponent::ComponentStartup()
{
    ILogStream() << "Wallet component startup";

    if (Args().GetArg<bool>("-disablewallet", DEFAULT_DISABLE_WALLET))
    {
        NLogFormat("Wallet disabled!");
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
        GetApp()->GetScheduler().scheduleEvery(std::bind(MaybeCompactWalletDB, vpWallets), 500);
    }

    return true;
}

bool CWalletComponent::ComponentShutdown()
{
    ILogStream() << "Wallet component shutdown";

    for (CWalletRef pWallet : vpWallets)
    {
        pWallet->Flush(false);
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


