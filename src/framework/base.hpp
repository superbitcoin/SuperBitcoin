#pragma once

#include <map>
#include <memory>
#include <thread>
#include "icomponent.h"
#include "utils/iterator.h"
#include "ui_interface.h"
#include "scheduler.h"
#include "interface/componentid.h"
#include "eventmanager/eventmanager.h"

class CArgsManager;
class CChainParams;
class CBaseChainParams;
class ECCVerifyHandle;

namespace appbase
{
    class CApp : public IComponent
    {
    public:
        static CApp &Instance();

        static log4cpp::Category &mlog;

        // the following four override methods were meaningless,
        // only just make CApp become a non-abstract class.
        int GetID() const override { return CID_APP; }

        state GetState() const override { return initialized; }

        bool Initialize() override { return false; }

        log4cpp::Category &getLog() override { return mlog; }



        uint64_t Version() const { return nVersion; }

        void SetVersion(uint64_t version) { nVersion = version; }

        void RequestShutdown() { bShutdown = true; }

        bool ShutdownRequested() { return bShutdown; }



        bool Initialize(int argc, char **argv);

        bool Startup() override;

        bool Shutdown() override;

        bool Run();

        bool RegisterComponent(IComponent* component);


        template<typename Component>
        Component* FindComponent() const
        {
            auto it = m_mapComponents.find(Component::ID);
            if (it != m_mapComponents.end())
            {
                return static_cast<Component*>(it->second.get());
            }
            return nullptr;
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
                        if (breakIfFailed)
                        {
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

        CScheduler& GetScheduler()
        {
            return *scheduler.get();
        }

        CEventManager& GetEventManager()
        {
            return *eventManager.get();
        }

        CClientUIInterface& GetUIInterface()
        {
            return *uiInterface.get();
        }

    private:
        CApp();
        CApp(const CApp& ) = delete;
        CApp& operator=(const CApp& ) = delete;

        void InitParameterInteraction();
        bool AppInitBasicSetup();
        bool AppInitParameterInteraction();
        bool AppInitSanityChecks();
        bool AppInitLockDataDirectory();

        bool AppInitialize(int argc, char *argv[]);
        bool ComponentInitialize();

    private:
        uint64_t nVersion;
        volatile bool bShutdown;
        std::unique_ptr<CArgsManager> cArgs;
        std::unique_ptr<CChainParams> cChainParams;
        std::unique_ptr<CBaseChainParams> cBaseChainParams;

        std::thread schedulerThread;
        std::unique_ptr<CScheduler>    scheduler;
        std::unique_ptr<CEventManager> eventManager;
        std::unique_ptr<CClientUIInterface> uiInterface;
        std::unique_ptr<ECCVerifyHandle> globalVerifyHandle;

        std::map<int, std::unique_ptr<IComponent>> m_mapComponents; ///< all registered plugins ordered by id.
    };
}

