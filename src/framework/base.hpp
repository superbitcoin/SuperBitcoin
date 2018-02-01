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
#ifndef BITCOIN_BASE_H
#define BITCOIN_BASE_H

#include <map>
#include <vector>
#include <memory>
#include <boost/core/demangle.hpp>
#include "basecomponent.hpp"

class CArgsManager;
class CChainParams;
class CBaseChainParams;

namespace appbase
{
    class CBase
    {
    public:
        static CBase &Instance();

        uint64_t Version() const;

        void SetVersion(uint64_t version);

        void Startup();

        void Shutdown();

        void Run();

        void Quit();


        template<typename Component>
        Component &RegisterComponent()
        {
            auto existing = FindComponent<Component>();
            if (existing)
                return *existing;

            auto _component = new Component();
            m_mapComponents[_component->Name()].reset(_component);
            return *_component;
        };

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

        template<typename Component>
        Component *FindComponent() const
        {
            std::string name = boost::core::demangle(typeid(Component).name());
            return static_cast<Component*>(FindComponent(name));
        };

        CArgsManager* GetArgsManager() { return cArgs.get(); }

        CChainParams* GetChainParams() { return cChainParams.get(); }

        CBaseChainParams* GetBaseChainParams() { return cBaseChainParams.get(); }

    private:

        CBase(); ///< private because CBase is a singleton that should be accessed via instance()

        ~CBase();

        CBase(const CBase& ) = delete;

        CBase& operator=(const CBase& ) = delete;


        bool InitializeImpl(int argc, char **argv, std::vector<CBaseComponent *> autostart_components);

        bool initParams(int argc, char *argv[]);

        CBaseComponent *FindComponent(const std::string &name) const;

    private:

        std::map<std::string, std::unique_ptr<CBaseComponent>> m_mapComponents; ///< all registered plugins
        std::vector<CBaseComponent *> m_vecInitializedComponents; ///< stored in the order they were started running
        std::vector<CBaseComponent *> m_vecRunningComponents; ///< stored in the order they were started running

        std::unique_ptr<CArgsManager> cArgs;
        std::unique_ptr<CChainParams> cChainParams;
        std::unique_ptr<CBaseChainParams> cBaseChainParams;

        uint64_t nVersion;
    };

    CBase &app();
}


#endif // !defined(BITCOIN_BASE_H)