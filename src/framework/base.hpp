#pragma once

#include <map>
#include <atomic>
#include <memory>
#include <interface/componentid.h>
#include <thread>
#include <eventmanager/eventmanager.h>
#include "icomponent.h"
#include "utils/iterator.h"
#include "ui_interface.h"
#include "scheduler.h"

class CArgsManager;
class CChainParams;
class CBaseChainParams;

namespace appbase
{
    class CApp:public IComponent
    {
    public:
        static CApp &Instance();

        enum { ID = CID_APP };
        virtual int GetID() const override { return ID; }


        uint64_t Version() const;

        void SetVersion(uint64_t version);

        bool Initialize(int argc, char **argv);

        virtual  bool Startup() override ;

        virtual bool Shutdown() override ;

        void Run();

        void RequestShutdown() { bShutdown = true; }

        bool RegisterComponent(IComponent* component);



        state GetState() const override;

        bool Initialize() override;

        log4cpp::Category &getLog() override;


        template<typename Component>
        Component* FindComponent() const
        {
            return static_cast<Component*>(FindComponent(Component::ID));
        };

        template<typename F, template<typename C> class CI = ContainerIterator>
        bool ForEachComponent(bool breakIfFailed, F&& func)
        {
            bool isOk = true;
            auto it = MakeContainerIterator<CI>(m_mapComponents);
            for (; !it.IsEnd(); it.Next())
            {
                if (IComponent* component = it->second.get())
                {
                    if (!func(component))
                    {
                        isOk = false;
                        if (breakIfFailed) {
                            break;
                        }
                    }
                }
            }
            return isOk;
        }

        const CArgsManager &GetArgsManager() const
        {
            return *cArgs.get();
        }

        const CChainParams &GetChainParams() const
        {
            return *cChainParams.get();
        }

        const CBaseChainParams &GetBaseChainParams() const
        {
            return *cBaseChainParams.get();
        }

    private:

        CApp(); ///< private because CBase is a singleton that should be accessed via instance()

        ~CApp();

        CApp(const CApp& ) = delete;

        CApp& operator=(const CApp& ) = delete;

        bool PreInit();
        bool InitParams(int argc, char *argv[]);

        IComponent* FindComponent(int componentID) const;

    private:
        uint64_t nVersion;
        std::atomic<bool> bShutdown;
        std::unique_ptr<CArgsManager> cArgs;
        std::unique_ptr<CChainParams> cChainParams;
        std::unique_ptr<CBaseChainParams> cBaseChainParams;

        std::map<int, std::unique_ptr<IComponent>> m_mapComponents; ///< all registered plugins ordered by id.

    public:

        CScheduler& GetScheduler() ;

        CEventManager& GetEventManager() ;

        CClientUIInterface& GetUIInterface();


    private:
        std::thread schedulerThread;
        std::unique_ptr<CScheduler>    scheduler;
        std::unique_ptr<CEventManager> eventManager;
        std::unique_ptr<CClientUIInterface> uiInterface;

    public:
        static log4cpp::Category &mlog;
    };



}

