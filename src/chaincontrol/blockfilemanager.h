///////////////////////////////////////////////////////////
//  CBlockFileManager.h
//  Implementation of the Class CBlockFileManager
//  Created on:      5-2-2018 16:40:57
//  Original author: marco
///////////////////////////////////////////////////////////

#ifndef __SBTC_BLOCKFILEMANAGER_H__
#define __SBTC_BLOCKFILEMANAGER_H__

#include "framework/sync.h"
#include "chain.h"
#include "utils/fs.h"

class CBlockFileManager
{
public:
    void Flush(int iLastBlockFile, std::vector<CBlockFileInfo> vecInfoBlockFile, bool bFinalize = false);

private:
    CCriticalSection csLastBlockFile;

    FILE *OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly);
};

#endif // !defined(__SBTC_BLOCKFILEMANAGER_H__)

