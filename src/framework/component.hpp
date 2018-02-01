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

#include <cassert>
#include <boost/core/demangle.hpp>
#include "basecomponent.hpp"

namespace appbase
{

    template<typename Impl>
    class CComponent : public CBaseComponent
    {
    public:
        CComponent() : _name(boost::core::demangle(typeid(Impl).name())) {}

        virtual ~CComponent() {}

        virtual state GetState() const override
        {
            return _state;
        }

        virtual const std::string &Name() const override
        {
            return _name;
        }

        virtual bool Initialize() override
        {
            if (_state == registered) {
                _state = initialized;
                static_cast<Impl*>(this)->ComponentInitialize();
                return true;
            }
            assert(_state == initialized); /// if initial state was not registered, final state cannot be initiaized
            return false; // means initialized repeatly.
        }

        virtual bool Startup() override
        {
            if (_state == initialized) {
                _state = started;
                static_cast<Impl*>(this)->ComponentStartup();
                return true;
            }
            assert(_state == started); // if initial state was not initialized, final state cannot be started
            return false;
        }

        virtual bool Shutdown() override
        {
            if (_state == started) {
                _state = stopped;
                static_cast<Impl*>(this)->ComponentShutdown();
                return true;
            }
            return false;
        }

    protected:
        CComponent(const std::string &name) : _name(name) {}

    private:
        state _state = CBaseComponent::registered;
        std::string _name;
    };
}