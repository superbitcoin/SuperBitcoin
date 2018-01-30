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

#include "base.hpp"
#include "basecomponent.hpp"

namespace appbase {
    template<typename Impl>
    class CComponent : public CBaseComponent {
    public:
        CComponent() : _name(boost::core::demangle(typeid(Impl).name())) {}

        virtual ~CComponent() {}

        virtual state GetState() const override { return _state; }

        virtual const std::string &Name() const override { return _name; }


        virtual void Initialize() override {
            if (_state == registered) {
                _state = initialized;
                static_cast<Impl *>(this)->ComponentInitialize();
                //ilog( "initializing CComponent ${name}", ("name",name()) );
                CBase::Instance().ComponentInitialized(*this);
            }
            assert(_state == initialized); /// if initial state was not registered, final state cannot be initiaized
        }

        virtual void Startup() override {
            if (_state == initialized) {
                _state = started;
                static_cast<Impl *>(this)->ComponentStartup();
                CBase::Instance().ComponentStarted(*this);
            }
            assert(_state == started); // if initial state was not initialized, final state cannot be started
        }

        virtual void Shutdown() override {
            if (_state == started) {
                _state = stopped;
                //ilog( "shutting down CComponent ${name}", ("name",name()) );
                static_cast<Impl *>(this)->ComponentShutdown();
            }
        }

    protected:
        CComponent(const string &name) : _name(name) {}

    private:
        state _state = CBaseComponent::registered;
        std::string _name;
    };
}