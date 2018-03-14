// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)

#include "config/sbtc-config.h"

#endif

#include <map>

#include "baseimpl.hpp"
#include "config/chainparams.h"
#include "sbtccore/clientversion.h"
#include "fs.h"
#include "utils/util.h"

#include <stdio.h>
#include <iostream>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>

using namespace std;
namespace bpo = boost::program_options;
using std::string;

static const int CONTINUE_EXECUTION = -1;

//
// This function returns either one of EXIT_ codes when it's expected to stop the process or
// CONTINUE_EXECUTION when it's expected to continue further.
//
void PrintVersion()
{
    std::cout << strprintf(_("%s RPC client version"), _(PACKAGE_NAME)) + " " + FormatFullVersion() + "\n" << std::endl;
}

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