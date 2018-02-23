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
#include "framework/init.h"
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>
#include <framework/base.hpp>


namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;
using std::vector;
using std::string;

inline appbase::CApp &app()
{
    return appbase::CApp::Instance();
}
inline log4cpp::Category &mlog() {
    return appbase::CApp::Instance().mlog;
}



#define _TXT__(x) #x
#define EMTOSTR(EM) _TXT__(EM)


// Application startup time (used for uptime calculation)
int64_t GetStartupTime();

static const bool DEFAULT_LOGTIMEMICROS = false;
static const bool DEFAULT_LOGIPS = false;
static const bool DEFAULT_LOGTIMESTAMPS = true;
static const bool DEFAULT_LOGFILEINFO = true;

/** Signals for translation. */
class CTranslationInterface
{
public:
    /** Translate a message to the native language of the user. */
    boost::signals2::signal<std::string(const char *psz)> Translate;
};

extern bool fPrintToConsole;
extern bool fPrintToDebugLog;

extern bool fLogTimestamps;
extern bool fLogTimeMicros;
extern bool fLogIPs;
extern std::atomic<bool> fReopenDebugLog;
extern CTranslationInterface translationInterface;

extern const char *const BITCOIN_CONF_FILENAME;
extern const char *const BITCOIN_PID_FILENAME;

extern std::atomic<uint32_t> logCategories;

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

struct CLogCategoryActive
{
    std::string category;
    bool active;
};

namespace BCLog
{
    enum LogFlags : uint32_t
    {
        NONE = 0,
        NET = (1 << 0),
        TOR = (1 << 1),
        MEMPOOL = (1 << 2),
        HTTP = (1 << 3),
        BENCH = (1 << 4),
        ZMQ = (1 << 5),
        DB = (1 << 6),
        RPC = (1 << 7),
        ESTIMATEFEE = (1 << 8),
        ADDRMAN = (1 << 9),
        SELECTCOINS = (1 << 10),
        REINDEX = (1 << 11),
        CMPCTBLOCK = (1 << 12),
        RAND = (1 << 13),
        PRUNE = (1 << 14),
        PROXY = (1 << 15),
        MEMPOOLREJ = (1 << 16),
        LIBEVENT = (1 << 17),
        COINDB = (1 << 18),
        QT = (1 << 19),
        LEVELDB = (1 << 20),
        CKECKPOINT = (1 << 21),
        ALL = ~(uint32_t)0,
    };
}

/** Return true if log accepts specified category */
static inline bool LogAcceptCategory(uint32_t category)
{
    return (logCategories.load(std::memory_order_relaxed) & category) != 0;
}

/** Returns a string with the log categories. */
std::string ListLogCategories();

/** Returns a vector of the active log categories. */
std::vector<CLogCategoryActive> ListActiveLogCategories();

/** Return true if str parses as a log category and set the flags in f */
bool GetLogCategory(uint32_t *f, const std::string *str);

/** Send a string to the log output */
int LogPrintStr(const std::string &str);

/** Get format string from VA_ARGS for error reporting */
template<typename... Args>
std::string FormatStringFromLogArgs(const char *fmt, const Args &... args)
{
    return fmt;
}

static inline void MarkUsed()
{
}

template<typename T, typename... Args>
static inline void MarkUsed(const T &t, const Args &... args)
{
    (void)t;
    MarkUsed(args...);
}

#ifdef USE_COVERAGE
#define LogPrintf(...) do { MarkUsed(__VA_ARGS__); } while(0)
#define LogPrint(category, ...) do { MarkUsed(__VA_ARGS__); } while(0)
#elif CHEAT_IDE
#define LogPrintf(...) do { MarkUsed(__VA_ARGS__); } while(0)
#define LogPrint(category, ...) do { MarkUsed(__VA_ARGS__); } while(0)
#else
#define LogPrintfFmt(...) do { \
    std::string _log_msg_; /* Unlikely name to avoid shadowing variables */ \
    try { \
        _log_msg_ = tfm::format(__VA_ARGS__); \
    } catch (tinyformat::format_error &fmterr) { \
        /* Original format string will have newline so don't add one here */ \
        _log_msg_ = "Error \"" + std::string(fmterr.what()) + "\" while formatting log message: " + FormatStringFromLogArgs(__VA_ARGS__); \
    } \
    LogPrintStr(_log_msg_); \
} while(0)
//;
#define LogPrintfWithFileInfo(fmt1, fmt2, a1, a2, a...) do{ \
    bool fileinfo = gArgs.GetArg<bool>("-logfileinfo", true) ; \
    if(fileinfo){ \
        LogPrintfFmt(fmt1, a1, a2, ##a); \
    }else{ \
        LogPrintfFmt(fmt2, ##a); \
    } \
} while(0)

#define LogPrintf(fmt, a...) LogPrintfWithFileInfo("%s(%d)" fmt, fmt, __FILE__, __LINE__, ##a)

#define LogPrint(category, ...) do { \
    if (LogAcceptCategory((category))) { \
        LogPrintf(__VA_ARGS__); \
    } \
} while(0)

#endif


template<typename... Args>
bool error(const char *fmt, const Args &... args)
{
    LogPrintStr("ERROR: " + tfm::format(fmt, args...) + "\n");
    return false;
}

void PrintExceptionContinue(const std::exception *pex, const char *pszThread);

void FileCommit(FILE *file);

bool TruncateFile(FILE *file, unsigned int length);

int RaiseFileDescriptorLimit(int nMinFD);

void AllocateFileRange(FILE *file, unsigned int offset, unsigned int length);

bool RenameOver(fs::path src, fs::path dest);

bool TryCreateDirectories(const fs::path &p);

fs::path GetDefaultDataDir();

const fs::path &GetDataDir(bool fNetSpecific = true);

void ClearDatadirCache();

fs::path GetConfigFile(const std::string &confPath);

#ifndef WIN32

fs::path GetPidFile();

void CreatePidFile(const fs::path &path, pid_t pid);

#endif
#ifdef WIN32
fs::path GetSpecialFolderPath(int nFolder, bool fCreate = true);
#endif

void OpenDebugLog();

void ShrinkDebugFile();

void runCommand(const std::string &strCommand);

inline bool IsSwitchChar(char c)
{
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

class ArgsManager
{
private:
    // if the option has multiple arguments, add to this arr
    vector<string> options_arr;

private:
    bool merge_variable_map(bpo::variables_map &desc, bpo::variables_map &source);

    const std::string SubPrefix(std::string str);

protected:
    CCriticalSection cs_args;
    std::map<std::string, std::string> mapArgs;
    std::map<std::string, std::vector<std::string> > mapMultiArgs;

    /************reconfiguration****************/
    bpo::options_description *app;
    bpo::variables_map vm;
    //    std::string version;
    /************reconfiguration****************/
public:
    ArgsManager()
    {
        options_arr =
                {
                        "loadblock",
                        "addnode",
                        "bind",
                        "connect",
                        "externalip",
                        "onlynet",
                        "seednode",
                        "whitebind",
                        "whitelist",
                        "wallet",
                        "uacomment",
                        "vbparams",
                        "debug",
                        "debugexclude",
                        "rpcbind",
                        "rpcauth",
                        "rpcallowip"
                };

    }

    static bool InterpretBool(const std::string &strValue);

    void ParseParameters(int argc, const char *const argv[]);

    void ReadConfigFile(const std::string &confPath);

    std::vector<std::string> GetArgs(const std::string &strArg);
    //    const std::string& GetVersion() const { return version; }

    bool PrintHelpMessage(std::function<void(void)>);   // because of compilation

    /**
     * Return true if initialize program options seccess
     *
     * @param callback Argument to execute initialization
     * @param argc the number of arguments
     * @param argv arguments
     * @param mode The help message mode determines what help message to show
     * @return true if the argument has been set
     */
    bool InitPromOptions(
            std::function<void(bpo::options_description *app, bpo::variables_map &vm, int argc, const char **argv,
                               HelpMessageMode mode)> callback, bpo::options_description *app, int argc,
            const char **argv, HelpMessageMode mode);

    /**
     * Return true if the given argument has been manually set
     *
     * @param strArg Argument to get (e.g. "--foo")
     * @return true if the argument has been set
     */
    bool IsArgSet(const std::string &strArg);

    /**
     * Return string argument or default value
     *
     * @param strArg Argument to get (e.g. "--foo")
     * @param strDefault (e.g. "1")
     * @return command-line argument or default value
     */

    template<class T>
    const T GetArg(const std::string &strArg, T const &tDefault)
    {
        LOCK(cs_args);
        std::string tmp_strArg = SubPrefix(strArg);
        if (vm.count(tmp_strArg))
        {
            if (typeid(T) == typeid(bool))
            {
                std::string str = vm[tmp_strArg].as<std::string>();
                bool te = InterpretBool(str);
                T *pte = (T *)&te;
                return *pte;
            } else
            {
                return vm[tmp_strArg].as<T>();
            }
        }
        return tDefault;
    };

    //    template <class T ,class  T2>
    //    const T GetArg(const T2 strArg, const T& strDefault);
    /**
     * Set an argument if it doesn't already have a value
     *
     * @param strArg Argument to set (e.g. "--foo")
     * @param strValue Value (e.g. "1")
     * @return true if argument gets set, false if it already had a value
     */
    template<typename T>
    bool SoftSetArg(const std::string &strArg, const T &value)
    {
        LOCK(cs_args);

        std::string tmp_strArg = SubPrefix(strArg);
        if (vm.count(tmp_strArg))
        {
            return false;
        }

        if (typeid(T) == typeid(bool))
        {
            bool const *const pbool = (bool const *const)&value;
            string value_tmp = *pbool ? "yes" : "no";
            vm.insert(std::make_pair(tmp_strArg, bpo::variable_value(boost::any(string(value_tmp)), false)));
            return true;
        }

        if (typeid(T) == typeid(std::string))
        {
            vector<string>::iterator ite = find(options_arr.begin(), options_arr.end(), tmp_strArg);

            // not an array
            if (ite == options_arr.end())
            {
                auto res = vm.insert(std::make_pair(tmp_strArg, bpo::variable_value(boost::any(T(value)),
                                                                                    false)));    // std::pair< map<string, bpo::variable_value>::iterator, bool >
                return res.second;
            }

            // the option is an array
            auto res = vm.insert(std::make_pair(tmp_strArg, bpo::variable_value(boost::any(vector<T>({value})),
                                                                                false)));   // std::pair< map<string, bpo::variable_value>::iterator, bool >
            return res.second;
        }

        vm.insert(std::make_pair(tmp_strArg, bpo::variable_value(boost::any(T(value)), false)));
        return true;
    }

    //    bool SoftSetArg(const std::string& strArg, const std::string& strValue);
    //
    //    /**
    //     * Set an argument if it doesn't already have a value
    //     *
    //     * @param strArg Argument to set (e.g. "--foo")
    //     * @param strValue Value (e.g. "1")
    //     * @return true if argument gets set, false if it already had a value
    //     */
    //    bool SoftSetArg(const std::string& strArg, const uint64_t& intValue);
    //
    //    /**
    //     * Set an argument if it doesn't already have a value
    //     *
    //     * @param strArg Argument to set (e.g. "--foo")
    //     * @param strValue Value (e.g. 1)
    //     * @return true if argument gets set, false if it already had a value
    //     */
    //    bool SoftSetArg(const std::string& strArg, const int32_t& value);
    //
    //    /**
    //     * Set an argument if it doesn't already have a value
    //     *
    //     * @param strArg Argument to set (e.g. "--foo")
    //     * @param strValue Value (e.g. 1)
    //     * @return true if argument gets set, false if it already had a value
    //     */
    //    bool SoftSetArg(const std::string& strArg, const uint32_t& intValue);
    //
    //    /**
    //     * Set an argument if it doesn't already have a value
    //     *
    //     * @param strArg Argument to set (e.g. "--foo")
    //     * @param strValue Value (e.g. 1L)
    //     * @return true if argument gets set, false if it already had a value
    //     */
    //    bool SoftSetArg(const std::string& strArg, const int64_t& value);
    //
    //    /**
    //     * Set an argument if it doesn't already have a value
    //     *
    //     * @param strArg Argument to set (e.g. "--foo")
    //     * @param strValue Values
    //     * @return true if argument gets set, false if it already had a value
    //     */
    //    bool SoftSetArg(const std::string& strArg, const std::vector< std::string >& value);
    //
    //    /**
    //     * Set a boolean argument if it doesn't already have a value
    //     *
    //     * @param strArg Argument to set (e.g. "--foo")
    //     * @param fValue Value (e.g. false)
    //     * @return true if argument gets set, false if it already had a value
    //     */
    //    bool SoftSetArg(const std::string &strArg, bool fValue);

    void ForceSetArg(const std::string &, const std::string &);

    void ForceSetArg(const std::string &, const unsigned int);
};

extern ArgsManager gArgs;

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
    std::string s = strprintf("sbtc-%s", name);
    RenameThread(s.c_str());
    try
    {
        mlog().info("%s thread start\n", name);
        func();
        mlog().info("%s thread exit\n", name);
    }
    catch (const boost::thread_interrupted &)
    {
        mlog().info("%s thread interrupt\n", name);
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


void
GenerateOptFormat(const int &argc, const char **argv, vector<string> &argv_arr_tmp, vector<const char *> &argv_arr);

std::string CopyrightHolders(const std::string &strPrefix);

#endif // BITCOIN_UTIL_H
