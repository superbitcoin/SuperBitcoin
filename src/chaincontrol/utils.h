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

#include <string>

/** Minimum disk space required - used in CheckDiskSpace() */
static const uint64_t nMinDiskSpace = 52428800;

/** Check whether enough disk space is available for an incoming block */
bool CheckDiskSpace(uint64_t nAdditionalBytes = 0);

#endif // !defined(__SBTC_CHAINCONTROL_UTILS_H__)

