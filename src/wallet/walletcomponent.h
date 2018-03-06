/*************************************
 * File name:       walletcomponent.h
 * Author:          Adolph
 * Date:            2018.2.1
 * Description:     Define the wallet component common interface
 *
 * History:         Record the edit history
 ************************************/
#ifndef SUPERBITCOIN_WALLETCOMPONENT_H
#define SUPERBITCOIN_WALLETCOMPONENT_H

#include <vector>
#include "interface/iwalletcomponent.h"
#include "wallet.h"

class CWalletComponent : public IWalletComponent
{
public:
    CWalletComponent();

    ~CWalletComponent();

    static log4cpp::Category &mlog;

    bool ComponentInitialize() override;

    bool ComponentStartup() override;

    bool ComponentShutdown() override;

    const char* whoru() const override;

    int GetWalletCount() const override;

    CWallet* GetWallet(int index) override;

    log4cpp::Category &getLog() override;

    template<typename F>
    void ForEachWallet(F&& func)
    {
        for (CWalletRef wallet : vpWallets)
        {
            if (func(wallet))
            {
                break;
            }
        }
    }

private:
    std::vector<CWalletRef> vpWallets;
};

#endif //SUPERBITCOIN_WALLETCOMPONENT_H
