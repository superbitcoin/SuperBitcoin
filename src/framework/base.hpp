#pragma once

#include <map>
#include <atomic>
#include <memory>
#include "icomponent.h"

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


        bool RegisterComponent(IComponent* component);

        template<typename Component>
        Component* FindComponent() const
        {
            return static_cast<Component*>(FindComponent(Component::ID));
        };

        CArgsManager* GetArgsManager() { return cArgs.get(); }

        CChainParams* GetChainParams() { return cChainParams.get(); }

        CBaseChainParams* GetBaseChainParams() { return cBaseChainParams.get(); }

        void RequestShutdown() { bShutdown = true; }

    private:

        CBase(); ///< private because CBase is a singleton that should be accessed via instance()

        ~CBase();

        CBase(const CBase& ) = delete;

        CBase& operator=(const CBase& ) = delete;

        bool InitParams(int argc, char *argv[]);

        IComponent* FindComponent(int componentID) const;

    private:
        uint64_t nVersion;
        std::atomic<bool> bShutdown;
        std::unique_ptr<CArgsManager> cArgs;
        std::unique_ptr<CChainParams> cChainParams;
        std::unique_ptr<CBaseChainParams> cBaseChainParams;

        std::map<int, std::unique_ptr<IComponent>> m_mapComponents; ///< all registered plugins ordered by id.
    };

    inline CBase &app()
    {
        return CBase::Instance();
    }
}

