/*************************************************
 * File name:		// basecomponent.hpp
 * Author:
 * Date: 		    //2018.01.26
 * Description:		// The base class of component implementation

 * Others:		    //
 * History:		    // 2018.01.26

 * 1. Date:
 * Author:
 * Modification:
*************************************************/
#pragma once

#include "basecomponent.hpp"

namespace appbase
{
    template<typename Impl>
    class CComponent : public CBaseComponent
    {
    public:
        virtual ~CComponent() {}

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
                return static_cast<Impl*>(this)->ComponentInitialize();
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
                return static_cast<Impl*>(this)->ComponentStartup();
            }
            return false;
        }

        virtual bool Shutdown() override
        {
            if (_state == stopped)
            {
                return true;
            }
            if (_state == started)
            {
                _state = stopped;
                return static_cast<Impl*>(this)->ComponentShutdown();
            }
            return false;
        }

    private:
        state _state = CBaseComponent::registered;
    };
}