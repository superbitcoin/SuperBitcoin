///////////////////////////////////////////////////////////
//  CBlockFileManager.cpp
//  Implementation of the Class CBlockFileManager
//  Created on:      5-2-2018 16:40:57
//  Original author: marco
///////////////////////////////////////////////////////////

#include "blockfilemanager.h"
#include "utils/tinyformat.h"
#include "utils/util.h"

fs::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix)
{

    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

static FILE *OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly)
{
    if (pos.IsNull())
        return nullptr;
    fs::path path = GetBlockPosFilename(pos, prefix);
    fs::create_directories(path.parent_path());
    FILE *file = fsbridge::fopen(path, "rb+");
    if (!file && !fReadOnly)
        file = fsbridge::fopen(path, "wb+");
    if (!file)
    {
        LogPrintf("Unable to open file %s\n", path.string());
        return nullptr;
    }
    if (pos.nPos)
    {
        if (fseek(file, pos.nPos, SEEK_SET))
        {
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
            fclose(file);
            return nullptr;
        }
    }
    return file;
}

/** Open an undo file (rev?????.dat) */
static FILE *OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly = false)
{
    return OpenDiskFile(pos, "rev", fReadOnly);
}

FILE *CBlockFileManager::OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly = false)
{
    return OpenDiskFile(pos, "blk", fReadOnly);
}

void CBlockFileManager::Flush(int iLastBlockFile, std::vector<CBlockFileInfo> vecInfoBlockFile, bool bFinalize)
{
    LOCK(csLastBlockFile);

    CDiskBlockPos posOld(iLastBlockFile, 0);

    FILE *fileOld = OpenBlockFile(posOld);
    if (fileOld)
    {
        if (bFinalize)
            TruncateFile(fileOld, vecInfoBlockFile[iLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld)
    {
        if (bFinalize)
            TruncateFile(fileOld, vecInfoBlockFile[iLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

