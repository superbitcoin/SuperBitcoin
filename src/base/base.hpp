#pragma once

#include <map>
#include <memory>
#include <thread>
#include <log4cpp/Category.hh>

#include "icomponent.h"
#include "utils/iterator.h"
#include "ui_interface.h"
#include "scheduler.h"
#include "interface/componentid.h"
#include "eventmanager/eventmanager.h"
#include "config/argmanager.h"

class CArgsManager;

class CChainParams;

class CBaseChainParams;

class ECCVerifyHandle;


namespace appbase
{
    class IBaseApp
    {
    public:

        virtual bool AppInitialize() = 0;

        virtual CScheduler &GetScheduler() = 0;

        virtual CEventManager &GetEventManager() = 0;

        virtual CClientUIInterface &GetUIInterface() = 0;

        IBaseApp();

        virtual bool Initialize(int argc, char **argv);

        static const CArgsManager &GetArgsManager()
        {
            return *pArgs.get();
        }

        static const CChainParams &GetChainParams()
        {
            return *pChainParams.get();
        }

        log4cpp::Category &getLog()
        {
            return mlog;
        }

        uint64_t Version() const
        {
            return nVersion;
        }

        void SetVersion(uint64_t version)
        {
            nVersion = version;
        }

        void RequestShutdown()
        {
            bShutdown = true;
        }

        bool ShutdownRequested()
        {
            return bShutdown;
        }

        virtual bool Startup();

        virtual bool Shutdown();

        bool Run();

        bool RegisterComponent(IComponent *component);


        template<typename Component>
        Component *FindComponent() const
        {
            auto it = m_mapComponents.find(Component::ID);
            if (it != m_mapComponents.end())
            {
                return static_cast<Component *>(it->second.get());
            }
            return nullptr;
        };

        template<typename F, template<typename C> class CI = ContainerIterator>
        bool ForEachComponent(bool breakIfFailed, F &&func)
        {
            bool isOk = true;
            auto it = MakeContainerIterator<CI>(m_mapComponents);
            for (; !it.IsEnd(); it.Next())
            {
                if (IComponent *component = it->second.get())
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

    public:
        static log4cpp::Category &mlog;
        uint64_t nVersion;
        volatile bool bShutdown;
    protected:
        virtual void InitOptionMap() = 0;

        bool InitializeLogging(fs::path path);

    protected:
        std::map<int, std::unique_ptr<IComponent>> m_mapComponents; ///< all registered plugins ordered by id.
        static std::unique_ptr<CArgsManager> pArgs;
        static std::unique_ptr<CChainParams> pChainParams;

    private:
        bool ComponentInitialize();

        bool ParamsInitialize(int argc, char **argv);

    };
}

const CArgsManager &Args();

const CChainParams &Params();

const CBaseChainParams &BaseParams();

appbase::IBaseApp *GetApp();

inline log4cpp::Category &mlog()
{
    return appbase::IBaseApp::mlog;
}