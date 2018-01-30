/*************************************************
 * File name:		// base.hpp
 * Author:
 * Date: 		    //2018.01.26
 * Description:		// Startup of the SBTC framework and the management of the components

 * Others:		    //
 * History:		    // 2018.01.26

 * 1. Date:
 * Author:
 * Modification:
*************************************************/
#pragma once

#ifndef BITCOIN_BASE_H
#define BITCOIN_BASE_H

#include <iostream>
#include <fstream>
#include "basecomponent.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/core/demangle.hpp>
#include <boost/asio.hpp>
#include <boost/thread/once.hpp>

#include "config/argmanager.h"
#include "config/chainparamsbase.h"
#include "config/chainparams.h"

namespace appbase
{
    //    namespace bpo = boost::program_options;
    //    namespace bfs = boost::filesystem;

    using boost::program_options::options_description;
    using boost::program_options::variables_map;
    using std::cout;

    class CBase
    {
    public:
        ~CBase();

        /** @brief Set version
         *
         * @param version Version output with -v/--version
         */
        void SetVersion(uint64_t version);

        /** @brief Get version
         *
         * @return Version output with -v/--version
         */
        uint64_t Version() const;

        /** @brief Get logging configuration path.
         *
         * @return Logging configuration location from command line
         */
        bfs::path GetLoggingConf() const;

        /**
         * @brief Looks for the --plugin commandline / config option and calls initialize on those plugins
         *
         * @tparam Plugin List of plugins to initalize even if not mentioned by configuration. For plugins started by
         * configuration settings or dependency resolution, this template has no effect.
         * @return true if the CBase and plugins were initialized, false or exception on error
         */
        template<typename... Component>
        bool Initialize(int argc, char **argv)
        {
            return InitializeImpl(argc, argv, {FindComponent<Component>()...});
        }

        bool initParams(int argc, char *argv[]);

        void Startup();

        void Shutdown();

        /**
         *  Wait until quit(), SIGINT or SIGTERM and then shutdown
         */
        void Run();

        void Quit();

        static CBase &Instance();

        CBaseComponent *FindComponent(const string &name) const;

        CBaseComponent &GetComponent(const string &name) const;

        template<typename Component>
        Component &RegisterComponent()
        {
            auto existing = FindComponent < Component > ();
            if (existing)
                return *existing;

            auto _component = new Component();
            m_mapComponents[_component->Name()].reset(_component);
            return *_component;
        };

        template<typename Component>
        Component *FindComponent() const
        {
            string name = boost::core::demangle(typeid(Component).name());
            return dynamic_cast<Component *>(FindComponent(name));
        };

        template<typename Component>
        Component &GetComponent() const
        {
            auto ptr = FindComponent<Component>();
            return *ptr;
        };

        bfs::path data_dir() const;

        CArgsManager cArgs;
        std::unique_ptr<class CChainParams> cChainParams;
        std::unique_ptr<class CBaseChainParams> cBaseChainParams;

    protected:
        template<typename Impl>
        friend
        class CComponent;

        bool InitializeImpl(int argc, char **argv, vector<CBaseComponent *> autostart_components);

        /** these notifications get called from the plugin when their state changes so that
         * the CBase can call shutdown in the reverse order.
         */
        ///@{
        void ComponentInitialized(CBaseComponent &component)
        {
            m_vecInitializedComponents.push_back(&component);
        }

        void ComponentStarted(CBaseComponent &component)
        {
            m_vecRunningComponents.push_back(&component);
        }
        ///@}

    private:
        CBase(); ///< private because CBase is a singleton that should be accessed via instance()
        map<string, std::unique_ptr<CBaseComponent>> m_mapComponents; ///< all registered plugins
        vector<CBaseComponent *> m_vecInitializedComponents; ///< stored in the order they were started running
        vector<CBaseComponent *> m_vecRunningComponents; ///< stored in the order they were started running

        void SetProgramOptions();

        void WriteDefaultConfig(const bfs::path &cfg_file);

        std::string ChainNameFromCommandLine();

        void SelectParams(const std::string &network);

        std::unique_ptr<class CBaseImpl> m_app_impl;

    };

    class CBaseImpl
    {
    public:
        CBaseImpl() : _app_options("Base Options")
        {
        }

        const variables_map *_options = nullptr;
        options_description _app_options;
        options_description _cfg_options;

        bfs::path _data_dir;
        bfs::path _logging_conf{"logging.json"};

        uint64_t _version;
    };


    CBase &app();
}


#endif // !defined(BITCOIN_BASE_H)