#pragma once

#include "icomponent.h"

namespace appbase
{
    template<typename Impl>
    class TComponent : public IComponent
    {
    public:
        virtual ~TComponent()
        {
        }

        virtual state GetState() const override
        {
            return _state;
        }

        virtual bool Initialize() override
        {
            if (_state == initialized)
            {
                return true;
            }
            if (_state == registered)
            {
                _state = initialized;
                return static_cast<Impl *>(this)->ComponentInitialize();
            }
            return false;
        }

        virtual bool Startup() override
        {
            if (_state == started)
            {
                return true;
            }
            if (_state == initialized)
            {
                _state = started;
                return static_cast<Impl *>(this)->ComponentStartup();
            }
            return false;
        }

        virtual bool Shutdown() override
        {
            if (_state == stopped)
            {
                return true;
            }
            if (_state != registered)
            {
                _state = stopped;
                return static_cast<Impl *>(this)->ComponentShutdown();
            }
            return false;
        }

        virtual log4cpp::Category &getLog() override
        {
            assert(nullptr == "this funtion must been never called");
            return log4cpp::Category::getRoot();
        };

        template<typename... Args>
        bool error(const char *fmt, const Args &... args)
        {
            getLog().error(fmt, args ...);
            return false;
        }

    private:
        state _state = IComponent::registered;
    };
}