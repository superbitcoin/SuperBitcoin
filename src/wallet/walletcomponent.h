#pragma once

#include <vector>
#include "interface/iwalletcomponent.h"
#include "wallet.h"

class CWalletComponent : public IWalletComponent
{
public:
    CWalletComponent();

    ~CWalletComponent();


    bool ComponentInitialize() override;

    bool ComponentStartup() override;

    bool ComponentShutdown() override;

    const char* whoru() const override;

    int GetWalletCount() const override;

    CWallet* GetWallet(int index) override;


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

