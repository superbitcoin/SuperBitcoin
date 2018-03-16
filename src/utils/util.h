// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Server/client environment: argument handling, config file parsing,
 * logging, thread wrappers, startup time
 */
#ifndef BITCOIN_UTIL_H
#define BITCOIN_UTIL_H

#if defined(HAVE_CONFIG_H)
#include "config/sbtc-config.h"
#endif
#include "base/base.hpp"
#include "compat/compat.h"
#include "fs.h"
#include "framework/sync.h"
#include "tinyformat.h"
#include "utiltime.h"

#include <atomic>
#include <exception>
#include <map>
#include <stdint.h>
#include <string>
#include <vector>

#include <boost/signals2/signal.hpp>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>
#include "config/argmanager.h"


namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;
using std::vector;
using std::string;

extern const char *const BITCOIN_CONF_FILENAME;
extern const char *const BITCOIN_PID_FILENAME;

// Application startup time (used for uptime calculation)
int64_t GetStartupTime();

/** Signals for translation. */
class CTranslationInterface
{
public:
    /** Translate a message to the native language of the user. */
    boost::signals2::signal<std::string(const char *psz)> Translate;
};

extern CTranslationInterface translationInterface;

/**
 * Translation function: Call Translate signal on UI interface, which returns a boost::optional result.
 * If no translation slot is registered, nothing is returned, and simply return the input.
 */
inline std::string _(const char *psz)
{
    boost::optional<std::string> rv = translationInterface.Translate(psz);
    return rv ? (*rv) : psz;
}


void SetupEnvironment();

bool SetupNetworking();

void PrintExceptionContinue(const std::exception *pex, const char *pszThread);

void FileCommit(FILE *file);

bool TruncateFile(FILE *file, unsigned int length);

int RaiseFileDescriptorLimit(int nMinFD);

void AllocateFileRange(FILE *file, unsigned int offset, unsigned int length);

bool RenameOver(fs::path src, fs::path dest);

bool TryCreateDirectories(const fs::path &p);

fs::path GetDefaultDataDir();

const fs::path &GetDataDir(bool fNetSpecific = true);

fs::path GetConfigFile(const std::string &confPath);

#ifndef WIN32

fs::path GetPidFile();

void CreatePidFile(const fs::path &path, pid_t pid);

#endif
#ifdef WIN32
fs::path GetSpecialFolderPath(int nFolder, bool fCreate = true);
#endif

void runCommand(const std::string &strCommand);

inline bool IsSwitchChar(char c)
{
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}


/**
 * Format a string to be used as group of options in help messages
 *
 * @param message Group name (e.g. "RPC server options:")
 * @return the formatted string
 */
std::string HelpMessageGroup(const std::string &message);

/**
 * Format a string to be used as option description in help messages
 *
 * @param option Option message (e.g. "-rpcuser=<user>")
 * @param message Option description (e.g. "Username for JSON-RPC connections")
 * @return the formatted string
 */
std::string HelpMessageOpt(const std::string &option, const std::string &message);

/**
 * Return the number of physical cores available on the current system.
 * @note This does not count virtual cores, such as those provided by HyperThreading
 * when boost is newer than 1.56.
 */
int GetNumCores();

void RenameThread(const char *name);

/**
 * .. and a wrapper that just calls func once
 */
template<typename Callable>
void TraceThread(const char *name, Callable func)
{
    SET_TEMP_LOG_CATEGORY(CID_APP);
    std::string s = strprintf("sbtc-%s", name);
    RenameThread(s.c_str());
    try
    {
        NLogFormat("%s thread start", name);
        func();
        NLogFormat("%s thread exit", name);
    }
    catch (const boost::thread_interrupted &)
    {
        NLogFormat("%s thread interrupt", name);
        throw;
    }
    catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, name);
        throw;
    }
    catch (...)
    {
        PrintExceptionContinue(nullptr, name);
        throw;
    }
}

std::string CopyrightHolders(const std::string &strPrefix);

#endif // BITCOIN_UTIL_H
