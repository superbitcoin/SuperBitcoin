//#include <application.hpp>
#include "base.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/asio/signal_set.hpp>

#include <iostream>
#include <fstream>

using namespace appbase;

static boost::once_flag onceFlag = BOOST_ONCE_INIT;

CBase::CBase() : m_app_impl(new CBaseImpl())
{
}

CBase::~CBase()
{

}

std::string CBase::ChainNameFromCommandLine()
{
    bool fRegTest = cArgs.IsArgSet("-regtest");
    bool fTestNet = cArgs.IsArgSet("-testnet");

    if (fTestNet && fRegTest)
        throw std::runtime_error("Invalid combination of -regtest and -testnet.");
    if (fRegTest)
        return CBaseChainParams::REGTEST;
    if (fTestNet)
        return CBaseChainParams::TESTNET;
    return CBaseChainParams::MAIN;
}

void CBase::SelectParams(const std::string &network)
{
    cBaseChainParams = CreateBaseChainParams(network);
    cChainParams = CreateChainParams(network);
}

bool CBase::initParams(int argc, char *argv[])
{
    if (!cArgs.Init(argc, argv))
    {
        return false;
    }
    try
    {
        if (!fs::is_directory(cArgs.GetDataDir(false)))
        {
            fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n",
                    cArgs.GetArg<std::string>("-datadir", "").c_str());
            return false;
        }
        try
        {
            cArgs.ReadConfigFile(cArgs.GetArg<std::string>("-conf", BITCOIN_CONF_FILENAME));
        } catch (const std::exception &e)
        {
            fprintf(stderr, "Error reading configuration file: %s\n", e.what());
            return false;
        }
        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        try
        {
            SelectParams(ChainNameFromCommandLine());
        } catch (const std::exception &e)
        {
            fprintf(stderr, "Error: %s\n", e.what());
            return false;
        }

        // -server defaults to true for bitcoind but not for the GUI so do this here
        cArgs.SoftSetArg("-server", true);
    } catch (const std::exception &e)
    {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...)
    {
        PrintExceptionContinue(nullptr, "AppInit()");
    }

    return true;
}

void CBase::SetVersion(uint64_t version)
{
    m_app_impl->_version = version;
}

uint64_t CBase::Version() const
{
    return m_app_impl->_version;
}

bfs::path CBase::GetLoggingConf() const
{
    return m_app_impl->_logging_conf;
}

void CBase::Startup()
{
    for (auto component : m_vecInitializedComponents)
        component->Startup();
}

CBase &CBase::Instance()
{
    static CBase _app;
    return _app;
}

CBase &app()
{
    return CBase::Instance();
}

void CBase::SetProgramOptions()
{
    for (auto &component : m_mapComponents)
    {
        boost::program_options::options_description component_cli_opts(
                "Command Line Options for " + component.second->Name());
        boost::program_options::options_description component_cfg_opts(
                "Config Options for " + component.second->Name());
        component.second->SetProgramOptions(component_cli_opts, component_cfg_opts);
        if (component_cfg_opts.options().size())
        {
            m_app_impl->_app_options.add(component_cfg_opts);
            m_app_impl->_cfg_options.add(component_cfg_opts);
        }
        if (component_cli_opts.options().size())
            m_app_impl->_app_options.add(component_cli_opts);
    }

    options_description app_cfg_opts("base Config Options");
    options_description app_cli_opts("base Command Line Options");
    app_cfg_opts.add_options()
            ("component", boost::program_options::value<vector<string> >()->composing(),
             "component(s) to enable, may be specified multiple times");

    app_cli_opts.add_options()
            ("help,h", "Print this help message and exit.")
            ("version,v", "Print version information.")
            ("data-dir,d", boost::program_options::value<bfs::path>()->default_value("data-dir"),
             "Directory containing configuration file config.ini")
            ("baseconfig,c", boost::program_options::value<bfs::path>()->default_value("baseconfig.ini"),
             "Configuration file name relative to data-dir")
            ("logconf,l", boost::program_options::value<bfs::path>()->default_value("logging.json"),
             "Logging configuration file name/path for library users");

    m_app_impl->_cfg_options.add(app_cfg_opts);
    m_app_impl->_app_options.add(app_cfg_opts);
    m_app_impl->_app_options.add(app_cli_opts);
}

bool CBase::InitializeImpl(int argc, char **argv, vector<CBaseComponent *> autostart_components)
{
    boost::call_once(onceFlag, &CBase::initParams, this, argc, argv);

    if (cArgs.IsArgSet("component"))
    {
//        auto components = options.at("component").as<std::vector<std::string>>();
        auto vComponent = cArgs.GetArgs("component");
        for (auto &arg : vComponent)
        {
            vector<string> names;
            boost::split(names, arg, boost::is_any_of(" \t,"));
            for (const std::string &name : names)
            {
                GetComponent(name).Initialize();
            }
        }
    }

    for (auto component : autostart_components)
    {
        if (component != nullptr && component->GetState() == CBaseComponent::registered)
        {
            component->Initialize();
        }
    }

    return true;
}

void CBase::Shutdown()
{
    for (auto ritr = m_vecRunningComponents.rbegin();
         ritr != m_vecRunningComponents.rend(); ++ritr)
    {
        (*ritr)->Shutdown();
    }
    for (auto ritr = m_vecRunningComponents.rbegin();
         ritr != m_vecRunningComponents.rend(); ++ritr)
    {
        m_mapComponents.erase((*ritr)->Name());
    }
    m_vecRunningComponents.clear();
    m_vecInitializedComponents.clear();
    m_mapComponents.clear();

}

void CBase::Quit()
{

}

void CBase::Run()
{

    /*while (1)
    {
        std::cout<<"loop...\n";
        sleep(100);
    }*/

    Shutdown(); /// perform synchronous shutdown
}

void CBase::WriteDefaultConfig(const bfs::path &cfg_file)
{
    if (!bfs::exists(cfg_file.parent_path()))
        bfs::create_directories(cfg_file.parent_path());

    std::ofstream out_cfg(bfs::path(cfg_file).make_preferred().string());
    for (const boost::shared_ptr<boost::program_options::option_description> od : m_app_impl->_cfg_options.options())
    {
        if (!od->description().empty())
            out_cfg << "# " << od->description() << "\n";
        boost::any store;
        if (!od->semantic()->apply_default(store))
            out_cfg << "# " << od->long_name() << " = \n";
        else
        {
            auto example = od->format_parameter();
            if (example.empty())
                // This is a boolean switch
                out_cfg << od->long_name() << " = " << "false\n";
            else
            {
                // The string is formatted "arg (=<interesting part>)"
                example.erase(0, 6);
                example.erase(example.length() - 1);
                out_cfg << od->long_name() << " = " << example << "\n";
            }
        }
        out_cfg << "\n";
    }
    out_cfg.close();
}

CBaseComponent *CBase::FindComponent(const string &name) const
{
    auto itr = m_mapComponents.find(name);
    if (itr == m_mapComponents.end())
    {
        return nullptr;
    }
    return itr->second.get();
}

CBaseComponent &CBase::GetComponent(const string &name) const
{
    auto ptr = FindComponent(name);
    if (!ptr)
    {
        std::cout << "unable to find component: " << name << std::endl;
        return *(CBaseComponent *)nullptr;
    }
    //if(!ptr)
    //BOOST_THROW_EXCEPTION(std::runtime_error("unable to find component: " + name));
    return *ptr;
}

bfs::path CBase::data_dir() const
{
    return m_app_impl->_data_dir;
}
