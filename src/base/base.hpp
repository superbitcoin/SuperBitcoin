#pragma once
#include <memory>
#include "icomponent.h"
#include "config/argmanager.h"

class CArgsManager;
class CChainParams;
class CScheduler;
class CEventManager;
class CClientUIInterface;

namespace appbase
{
    class IBaseApp
    {
    public:
        IBaseApp();


        virtual bool Initialize(int argc, char **argv);

        virtual bool Startup();

        virtual bool Run();

        virtual bool Shutdown();

        virtual CScheduler &GetScheduler();

        virtual CEventManager &GetEventManager();

        virtual CClientUIInterface &GetUIInterface();


        void RequestShutdown()
        {
            bShutdown = true;
        }

        bool ShutdownRequested()
        {
            return bShutdown;
        }

        template<typename Component>
        Component *FindComponent() const
        {
            if (auto ic = FindComponent(Component::ID))
            {
                return static_cast<Component*>(ic);
            }
            return nullptr;
        };

        uint64_t nVersion;
        static std::unique_ptr<CArgsManager> pArgs;
        static std::unique_ptr<CChainParams> pChainParams;

    protected:
        virtual void InitOptionMap() = 0;

        virtual void PrintAppStartupInfo();

        virtual IComponent *FindComponent(int id) const;

        volatile bool bShutdown;
    };
}

const CArgsManager &Args();

const CChainParams &Params();

appbase::IBaseApp *GetApp();


