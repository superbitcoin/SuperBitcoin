// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)

#include "config/sbtc-config.h"

#endif

#include "baseimpl.hpp"

CApp gApp;

appbase::IBaseApp *GetApp()
{
    return &gApp;
}

int main(int argc, char *argv[])
{
    gApp.Initialize(argc, argv) && gApp.Startup() && gApp.Run(argc, argv);
    gApp.Shutdown();

    return 0;
}