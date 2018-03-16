///////////////////////////////////////////////////////////
//  CBlockFileManager.cpp
//  Implementation of the Class CBlockFileManager
//  Created on:      5-2-2018 16:40:57
//  Original author: marco
///////////////////////////////////////////////////////////
#include "utils/util.h"
#include "blockfilemanager.h"
#include "utils/tinyformat.h"

#include "utils/utilstrencodings.h"
#include "sbtccore/streams.h"
#include "sbtccore/clientversion.h"

CCriticalSection csLastBlockFile;

fs::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix)
{
    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

FILE *OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly)
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
FILE *OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly)
{
    return OpenDiskFile(pos, "rev", fReadOnly);
}

FILE *OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly)
{
    return OpenDiskFile(pos, "blk", fReadOnly);
}

bool ReadBlockFromDisk(CBlock &block, const CDiskBlockPos &pos, const Consensus::Params &consensusParams)
{
    block.SetNull();

    // Open history file to read
    CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
    {
        mlog_error("ReadBlockFromDisk: OpenBlockFile failed for %s", pos.ToString());
        return false;
    }

    // Read block
    try
    {
        filein >> block;
    }
    catch (const std::exception &e)
    {
        mlog_error("%s: Deserialize or I/O error - %s at %s", __func__, e.what(), pos.ToString());
        return false;
    }

    // Check the header
    if (!CheckProofOfWork(block.GetHash(), block.nBits, consensusParams))
    {
        mlog_error("ReadBlockFromDisk: Errors in block header at %s", pos.ToString());
        return false;
    }
    return true;
}

bool ReadBlockFromDisk(CBlock &block, const CBlockIndex *pindex, const Consensus::Params &consensusParams)
{
    if (!ReadBlockFromDisk(block, pindex->GetBlockPos(), consensusParams))
        return false;
    if (block.GetHash() != pindex->GetBlockHash())
    {
        mlog_error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s",
                   pindex->ToString(), pindex->GetBlockPos().ToString());
        return false;
    }
    return true;
}

bool UndoReadFromDisk(CBlockUndo &blockundo, const CDiskBlockPos &pos, const uint256 &hashBlock)
{
    // Open history file to read
    CAutoFile filein(OpenUndoFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
    {
        mlog_error("%s: OpenUndoFile failed", __func__);
        return false;
    }

    // Read block
    uint256 hashChecksum;
    CHashVerifier<CAutoFile> verifier(&filein); // We need a CHashVerifier as reserializing may lose data
    try
    {
        verifier << hashBlock;
        verifier >> blockundo;
        filein >> hashChecksum;
    }
    catch (const std::exception &e)
    {
        mlog_error("%s: Deserialize or I/O error - %s", __func__, e.what());
        return false;
    }

    // Verify checksum
    if (hashChecksum != verifier.GetHash())
    {
        mlog_error("%s: Checksum mismatch", __func__);
        return false;
    }

    return true;
}

bool WriteBlockToDisk(const CBlock &block, CDiskBlockPos &pos, const CMessageHeader::MessageStartChars &messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenBlockFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
    {
        mlog_error("WriteBlockToDisk: OpenBlockFile failed");
        return false;
    }

    // Write index header
    unsigned int nSize = GetSerializeSize(fileout, block);
    fileout << FLATDATA(messageStart) << nSize;

    // Write block
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
    {
        mlog_error("WriteBlockToDisk: ftell failed");
        return false;
    }
    pos.nPos = (unsigned int)fileOutPos;
    fileout << block;

    return true;
}

bool UndoWriteToDisk(const CBlockUndo &blockundo, CDiskBlockPos &pos, const uint256 &hashBlock,
                     const CMessageHeader::MessageStartChars &messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenUndoFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
    {
        mlog_error("%s: OpenUndoFile failed", __func__);
        return false;
    }

    // Write index header
    unsigned int nSize = GetSerializeSize(fileout, blockundo);
    fileout << FLATDATA(messageStart) << nSize;

    // Write undo data
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
    {
        mlog_error("%s: ftell failed", __func__);
        return false;
    }
    pos.nPos = (unsigned int)fileOutPos;
    fileout << blockundo;

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    fileout << hasher.GetHash();

    return true;
}

void FlushBlockFile(int iLastBlockFile, int iSize, int iUndoSize, bool bFinalize)
{
    LOCK(csLastBlockFile);

    CDiskBlockPos posOld(iLastBlockFile, 0);

    FILE *fileOld = OpenBlockFile(posOld);
    if (fileOld)
    {
        if (bFinalize)
            TruncateFile(fileOld, iSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld)
    {
        if (bFinalize)
            TruncateFile(fileOld, iUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

// If we're using -prune with -reindex, then delete block files that will be ignored by the
// reindex.  Since reindexing works by starting at block file 0 and looping until a blockfile
// is missing, do the same here to delete any later block files after a gap.  Also delete all
// rev files since they'll be rewritten by the reindex anyway.  This ensures that vinfoBlockFile
// is in sync with what's actually on disk by the time we start downloading, so that pruning
// works correctly.
void CleanupBlockRevFiles()
{
    std::map<std::string, fs::path> mapBlockFiles;

    // Glob all blk?????.dat and rev?????.dat files from the blocks directory.
    // Remove the rev files immediately and insert the blk file paths into an
    // ordered map keyed by block file index.
    LogPrintf("Removing unusable blk?????.dat and rev?????.dat files for -reindex with -prune\n");
    fs::path blocksdir = GetDataDir() / "blocks";
    for (fs::directory_iterator it(blocksdir); it != fs::directory_iterator(); it++)
    {
        if (is_regular_file(*it) &&
            it->path().filename().string().length() == 12 &&
            it->path().filename().string().substr(8, 4) == ".dat")
        {
            if (it->path().filename().string().substr(0, 3) == "blk")
                mapBlockFiles[it->path().filename().string().substr(3, 5)] = it->path();
            else if (it->path().filename().string().substr(0, 3) == "rev")
                remove(it->path());
        }
    }

    // Remove all block files that aren't part of a contiguous set starting at
    // zero by walking the ordered map (keys are block file indices) by
    // keeping a separate counter.  Once we hit a gap (or if 0 doesn't exist)
    // start removing block files.
    int nContigCounter = 0;
    for (const std::pair<std::string, fs::path> &item : mapBlockFiles)
    {
        if (atoi(item.first) == nContigCounter)
        {
            nContigCounter++;
            continue;
        }
        remove(item.second);
    }
}

