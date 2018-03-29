///////////////////////////////////////////////////////////
//  minercomponent.h
//  Created on:      29-3-2018 09:57:57
//  Original author: marco
///////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>
#include <log4cpp/Category.hh>
#include "util.h"
#include "interface/iminercomponent.h"

class CMinerComponent : public IMinerComponent
{
public:
    CMinerComponent();

    ~CMinerComponent();

    bool ComponentInitialize() override;

    bool ComponentStartup() override;

    bool ComponentShutdown() override;

private:

    CCriticalSection cs;

    /** Run the miner threads */
    void GenerateBitcoins(bool fGenerate, int nThreads, const CChainParams &chainparams);

    void SbtcMiner(const CChainParams &chainparams);

    bool ProcessBlockFound(const CBlock *pblock, const CChainParams &chainparams);
};