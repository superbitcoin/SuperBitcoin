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
#include <memory>
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

        bool Initialize(int argc, char **argv);

        bool Startup();

        bool Shutdown();

        void Run();

        void Quit();


        bool RegisterComponent(CBaseComponent* component);

        template<typename Component>
        Component* FindComponent() const
        {
            return static_cast<Component*>(FindComponent(Component::ID));
        };

        CArgsManager* GetArgsManager() { return cArgs.get(); }

        CChainParams* GetChainParams() { return cChainParams.get(); }

        CBaseChainParams* GetBaseChainParams() { return cBaseChainParams.get(); }

    private:

        CBase(); ///< private because CBase is a singleton that should be accessed via instance()

        ~CBase();

        CBase(const CBase& ) = delete;

        CBase& operator=(const CBase& ) = delete;

        bool InitParams(int argc, char *argv[]);

        CBaseComponent* FindComponent(int componentID) const;

    private:

        uint64_t nVersion;

        std::unique_ptr<CArgsManager> cArgs;
        std::unique_ptr<CChainParams> cChainParams;
        std::unique_ptr<CBaseChainParams> cBaseChainParams;

        std::map<int, std::unique_ptr<CBaseComponent>> m_mapComponents; ///< all registered plugins ordered by id.
    };

    CBase &app();
}


#endif // !defined(BITCOIN_BASE_H)