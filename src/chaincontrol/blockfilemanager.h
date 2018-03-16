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
#include "p2p/protocol.h"
#include "sbtccore/block/undo.h"


extern CCriticalSection csLastBlockFile;

FILE *OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly = false);

fs::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix);

FILE *OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly);

FILE *OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly = false);

/** Functions for disk access for blocks */
bool ReadBlockFromDisk(CBlock &block, const CDiskBlockPos &pos, const Consensus::Params &consensusParams);

bool ReadBlockFromDisk(CBlock &block, const CBlockIndex *pindex, const Consensus::Params &consensusParams);

bool UndoReadFromDisk(CBlockUndo &blockundo, const CDiskBlockPos &pos, const uint256 &hashBlock);

bool WriteBlockToDisk(const CBlock &block, CDiskBlockPos &pos, const CMessageHeader::MessageStartChars &messageStart);

bool UndoWriteToDisk(const CBlockUndo &blockundo, CDiskBlockPos &pos, const uint256 &hashBlock,
                     const CMessageHeader::MessageStartChars &messageStart);

void FlushBlockFile(int iLastBlockFile, int iSize, int iUndoSize, bool bFinalize = false);

void CleanupBlockRevFiles();

#endif // !defined(__SBTC_BLOCKFILEMANAGER_H__)

