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

#include "component.hpp"
#include "rpcwallet.h"
#include "server.h"
#include "scheduler.h"
#include "../interface/iwalletcomponent.h"
#include "wallet.h"

using namespace appbase;

class CWalletComponent : public IWalletComponent
{
private:
    std::vector<CWalletRef> vpWallets;

public:
    CWalletComponent() {};

    ~CWalletComponent() {};

    bool ComponentInitialize() override;

    bool ComponentStartup() override;

    bool ComponentShutdown() override;

    virtual const char* whoru() const override { return "I am CWalletComponent\n"; }

    std::vector<CWalletRef>& GetWalletRef() { return vpWallets; }
};

#endif //SUPERBITCOIN_WALLETCOMPONENT_H
