///////////////////////////////////////////////////////////
//  argmanager.h
//  Implementation of the Class CArgsManager
//  Created on:      29-2-2018 11:38:04
//  Original author: marco
///////////////////////////////////////////////////////////

#ifndef __SBTC_CARGSMANAGER_H__
#define __SBTC_CARGSMANAGER_H__

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

#include "utils/fs.h"


//#include "chainparams.h"
//#include "chainparamsbase.h"
//#include "chaincontrol/checkpoints.h"

//#include "sbtccore/clientversion.h"
//#include "compat/compat.h"
//#include "framework/sync.h"
//#include "framework/init.h"
//#include "framework/noui.h"
//#include "framework/scheduler.h"
//#include "utils/util.h"
//#include "utils/net/httpserver.h"
//#include "utils/net/httprpc.h"
//#include "utils/utilstrencodings.h"


//#include "transaction/txdb.h"
//#include "p2p/net_processing.h"
//#include "sbtccore/transaction/policy.h"
//#include "block/validation.h"
//#include "p2p/netbase.h"
//#include "utils/net/torcontrol.h"
//#include "script/sigcache.h"
//#include "utils/utilmoneystr.h"
//#include "script/standard.h"
//#include "rpc/protocol.h"
//#include "wallet/wallet.h"
//#include "wallet/db.h"
//#include "wallet/walletdb.h"
//#include "framework/init.h"
//#include "rpc/protocol.h"

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;
using std::vector;
using std::string;

/** The help message mode determines what help message to show */
enum HelpMessageMode
{
    HMM_BITCOIND,
    HMM_BITCOIN_QT,
    HMM_EMPTY
};

class CArgsManager
{

public:
    virtual ~CArgsManager();

    CArgsManager()
    {

    }

    bool Init(int argc, char *argv[]);

    void ForceSetArg(const std::string &prm1, const std::string &prm2);

    void ForceSetArg(const std::string &prm1, const unsigned int prm2);

    template<class T>
    const T GetArg(const std::string &strArg, const T &tDefault) const
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
    }

    const std::vector<std::string> GetArgs(const std::string &strArg) const;

    void
    GenerateOptFormat(const int &argc, const char **argv, vector<string> &argv_arr_tmp, vector<const char *> &argv_arr);

    bool PreProc(std::function<void(bpo::options_description *app, bpo::variables_map &vm, int argc,
                                    const char **argv, HelpMessageMode mode)> callback,
                 bpo::options_description *app, int argc, const char **argv, HelpMessageMode mode);

    void InitPromOptions(bpo::options_description *app, bpo::variables_map &vm, int argc, const char **argv,
                         HelpMessageMode mode);

    static bool InterpretBool(const std::string &strValue);

    bool IsArgSet(const std::string &strArg) const;

    std::string GetHelpMessage() const;

    bool PrintHelpMessage(std::function<void(void)> callback);

    fs::path GetConfigFile(const std::string &confPath);

    void ReadConfigFile(const std::string &confPath);

    const fs::path &GetDataDir(bool fNetSpecific = true) const;

    template<class T>
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

protected:
    /**
     * reconfiguration
     */
    bpo::options_description *app_bpo;
    mutable CCriticalSection cs_args;
    std::map<std::string, std::string> mapArgs;
    std::map<std::string, std::vector<std::string> > mapMultiArgs;
    bpo::variables_map vm;

private:
    /**
     * if the option has multiple arguments, add to this arr
     */
    vector<string> options_arr;

    bool merge_variable_map(bpo::variables_map &desc, bpo::variables_map &source);

    const std::string SubPrefix(std::string str) const;

};

extern CArgsManager gArgs;

#endif // !defined(__SBTC_CARGSMANAGER_H__)
