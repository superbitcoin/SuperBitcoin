///////////////////////////////////////////////////////////
//  argmanager.cpp
//  Implementation of the Class CArgsManager
//  Created on:      29-2-2018 11:38:05
//  Original author: marco
///////////////////////////////////////////////////////////
#include <log4cpp/Category.hh>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>

#include "argmanager.h"
#include "chainparams.h"
#include "chaincontrol/checkpoints.h"

#include "sbtccore/clientversion.h"
#include "compat/compat.h"
#include "framework/sync.h"
#include "framework/noui.h"
#include "framework/scheduler.h"
#include "utils/util.h"
#include "utils/net/httpserver.h"
#include "utils/net/httprpc.h"
#include "utils/utilstrencodings.h"


#include "transaction/txdb.h"
#include "p2p/net_processing.h"
#include "sbtccore/transaction/policy.h"
#include "block/validation.h"
#include "p2p/netbase.h"
#include "utils/net/torcontrol.h"
#include "script/sigcache.h"
#include "utils/utilmoneystr.h"
#include "script/standard.h"
#include "rpc/protocol.h"
#include "wallet/wallet.h"
#include "wallet/db.h"
#include "wallet/walletdb.h"
#include "rpc/protocol.h"

CArgsManager gArgs;

CArgsManager::~CArgsManager()
{

}

void CArgsManager::SetOptionName(const std::string &optionName)
{
    this->optionName = optionName;
}

void CArgsManager::SetOptionTable(std::map<std::string, std::vector<option_item>> &optionTable)
{
    this->optionTable = std::move(optionTable);
}

void CArgsManager::ForceSetArg(const std::string &strArg, const std::string &strValue)
{
    LOCK(cs_args);
    std::string tmp_strArg = SubPrefix(strArg);

    vector<string>::const_iterator ite_options_arr = find(options_arr.begin(), options_arr.end(), tmp_strArg);

    if (ite_options_arr == options_arr.end())
    {
        vm.erase(tmp_strArg);
        vm.insert(std::make_pair(tmp_strArg, bpo::variable_value(boost::any(std::string(strValue)),
                                                                 false)));    // std::pair< map<string, bpo::variable_value>::iterator, bool >

        return;
    }

    vector<string> &tmp_value_arr = vm.at(tmp_strArg).as<vector<string> >();
    tmp_value_arr.insert(tmp_value_arr.end(), strValue);
}

void CArgsManager::ForceSetArg(const std::string &strArg, const unsigned int value)
{
    LOCK(cs_args);
    std::string tmp_strArg = SubPrefix(strArg);

    if (vm.count(tmp_strArg))
    {
        vm.erase(tmp_strArg);
    }

    vm.insert(std::make_pair(tmp_strArg, bpo::variable_value(boost::any((unsigned int)value), false)));
}


const std::vector<std::string> CArgsManager::GetArgs(const std::string &strArg) const
{
    LOCK(cs_args);
    std::string tmp_strArg = SubPrefix(strArg);

    if (IsArgSet(tmp_strArg))
        return vm.at(tmp_strArg).as<vector<string> >();
    return {};
}

bool CArgsManager::Init(int argc, char *argv[])
{
    LOCK(cs_args);
    vm.clear();
    app_bpo = new bpo::options_description(optionName);

    for (auto &groupItem : optionTable)
    {
        bpo::options_description group(groupItem.first);
        bpo::options_description_easy_init odei = group.add_options();
        for (auto &item: groupItem.second)
        {
            if (item.s && !item.description.empty())
            {
                odei(item.name, (const bpo::value_semantic *)item.s, item.description.c_str());
            } else if (item.s)
            {
                odei(item.name, (const bpo::value_semantic *)item.s);
            } else
            {
                odei(item.name, item.description.c_str());
            }
        }

        app_bpo->add(group);
    }

    // bpo::store(bpo::parse_command_line(argv_arr.size(), &argv_arr[0], *app_bpo), vm);

    vector<string> unrecognisedOptions;
    try
    {
        bpo::parsed_options parsed = bpo::command_line_parser(argc, (const char**)argv).options(*app_bpo).allow_unregistered().run();
        unrecognisedOptions = collect_unrecognized(parsed.options, bpo::include_positional);
        bpo::store(parsed, vm);
        bpo::notify(vm);
    }
    catch (bpo::error const& e)
    {
        std::cerr << e.what() << std::endl;
        return false;
    }

    if (!unrecognisedOptions.empty())
    {
        for (size_t i = 0; i < unrecognisedOptions.size(); ++i)
        {
            std::cerr << "Invalid argument: " << unrecognisedOptions[i] << std::endl;
        }
        return false;
    }

    return true;
}

/** Interpret string as boolean, for argument parsing */
bool CArgsManager::InterpretBool(const std::string &strValue)
{
    if (strValue.empty())
        return true;
    return (!strcmp(boost::to_lower_copy<std::string>(strValue).c_str(), "yes") ||
            !strcmp(boost::to_lower_copy<std::string>(strValue).c_str(), "y")
            || !strcmp(boost::to_lower_copy<std::string>(strValue).c_str(), "1") ||
            !strcmp(boost::to_lower_copy<std::string>(strValue).c_str(), "true")
           ) ? true : false;
}


bool CArgsManager::IsArgSet(const std::string &strArg) const
{
    LOCK(cs_args);
    std::string tmp_strArg = SubPrefix(strArg);

    return vm.count(tmp_strArg);
}


bool CArgsManager::merge_variable_map(bpo::variables_map &desc, bpo::variables_map &source)
{
    LOCK(cs_args);
    for (bpo::variables_map::iterator ite_src = source.begin(); ite_src != source.end(); ite_src++)
    {
        bpo::variables_map::iterator ite_desc = desc.find(ite_src->first);
        if (ite_desc != desc.end())  // find
        {
            vector<string>::const_iterator ite_options_arr = find(options_arr.begin(), options_arr.end(),
                                                                  ite_src->first);
            if (ite_options_arr != options_arr.end())    // value is array
            {
                vector<string> &desc_value = desc.at(ite_src->first).as<vector<string> >();
                desc_value.insert(desc_value.end(), ite_src->second.as<vector<string> >().begin(),
                                  ite_src->second.as<vector<string> >().end());

                // delete mutiple parameters
                sort(desc_value.begin(), desc_value.end());
                desc_value.erase(unique(desc_value.begin(), desc_value.end()), desc_value.end());
            } else
            {
                //value is basic data type, pass(use parameter from commond line)
            }
        } else    // not find
        {
            bpo::variable_value tmp_value = ite_src->second;
            auto res = desc.insert(std::make_pair(ite_src->first,
                                                  tmp_value)); // return type of insert is std::pair< map<string, bpo::variable_value>::iterator, bool >
            if (!res.second)
            {
                return false;
            }
        }
    }

    return true;
}

std::string CArgsManager::GetHelpMessage() const
{
    if (!app_bpo)
    {
        return std::string();
    }

    std::ostringstream oss;
    oss << *app_bpo << std::endl;
    return oss.str();
}

bool CArgsManager::PrintHelpMessage(std::function<void(void)> callback)
{
    if (vm.count("help"))
    {
        std::cout << *app_bpo << std::endl;
        return true;
    }

    if (vm.count("version"))
    {
        if (callback)
        {
            callback();
            return true;
        }
    }

    return false;
}

static fs::path pathCached;
static fs::path pathCachedNetSpecific;
static CCriticalSection csPathCached;

void CArgsManager::ClearDatadirCache() const
{
    LOCK(csPathCached);

    pathCached = fs::path();
    pathCachedNetSpecific = fs::path();
}

fs::path CArgsManager::GetConfigFile(const std::string &confPath) const
{
    fs::path pathConfigFile(confPath);
    if (!pathConfigFile.is_complete())
        pathConfigFile = GetDataDir(false) / pathConfigFile;

    return pathConfigFile;
}

void CArgsManager::ReadConfigFile(const std::string &confPath)
{
    bpo::variables_map vm_tmp;
    bfs::path config_file_name(GetConfigFile(confPath));
    bpo::store(bpo::parse_config_file<char>(config_file_name.make_preferred().string().c_str(), *app_bpo, true),
               vm_tmp);
    merge_variable_map(vm, vm_tmp);

    // If datadir is changed in .conf file:
    ClearDatadirCache();
}


const std::string CArgsManager::SubPrefix(std::string str) const
{
    std::string tmp_strArg;
    if (str[0] == '-')
    {
        tmp_strArg = str.substr(1);
    } else
    {
        tmp_strArg = str;
    }
    return tmp_strArg;
}

const fs::path &CArgsManager::GetDataDir(bool fNetSpecific) const
{

    LOCK(csPathCached);

    fs::path &path = fNetSpecific ? pathCachedNetSpecific : pathCached;

    // This can be called during exceptions by LogPrintf(), so we cache the
    // value so we don't have to do memory allocations after that.
    if (!path.empty())
        return path;

    if (IsArgSet("-datadir"))
    {
        std::string tmp = GetArg<std::string>("-datadir", "");
        path = fs::system_complete(GetArg<std::string>("-datadir", ""));
        if (!fs::is_directory(path))
        {
            path = "";
            return path;
        }
    } else
    {
        path = GetDefaultDataDir();
    }
    if (fNetSpecific)
        path /= Params().DataDir();

    fs::create_directories(path);

    return path;
}
