///////////////////////////////////////////////////////////
//  utils.h
//  Created on:      28-2-2018 11:02:57
//  Original author: marco
///////////////////////////////////////////////////////////

#ifndef __SBTC_CHAINCONTROL_UTILS_H__
#define __SBTC_CHAINCONTROL_UTILS_H__

#if defined(HAVE_CONFIG_H)

#include "config/sbtc-config.h"

#endif

#include "validation.h"
#include "chainparams.h"
#include "chain.h"

#include <string>

/** Minimum disk space required - used in CheckDiskSpace() */
static const uint64_t nMinDiskSpace = 52428800;

/** Check whether enough disk space is available for an incoming block */
bool CheckDiskSpace(uint64_t nAdditionalBytes = 0);

void AlertNotify(const std::string &strMessage);

/** Abort with a message */
bool AbortNode(const std::string &strMessage, const std::string &userMessage = "");

bool AbortNode(CValidationState &state, const std::string &strMessage, const std::string &userMessage = "");

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state);

/** Guess verification progress (as a fraction between 0.0=genesis and 1.0=current tip). */
double GuessVerificationProgress(const ChainTxData &data, CBlockIndex *pindex);

#endif // !defined(__SBTC_CHAINCONTROL_UTILS_H__)

