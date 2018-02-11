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
    fs::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix);

    FILE *OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly);

    FILE *OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly = false);

    void Flush(int iLastBlockFile, int iSize, int iUndoSize, bool bFinalize = false);

    void CleanupBlockRevFiles();

private:
    CCriticalSection csLastBlockFile;

    FILE *OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly);
};

#endif // !defined(__SBTC_BLOCKFILEMANAGER_H__)

