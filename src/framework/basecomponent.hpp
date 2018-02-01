/*************************************************
 * File name:		// basecomponent.hpp
 * Author:
 * Date: 		    //2018.01.26
 * Description:		// The base class of a component, and all components are derived from this class

 * Others:		    //
 * History:		    // 2018.01.26

 * 1. Date:
 * Author:
 * Modification:
*************************************************/
#pragma once

namespace appbase
{
    class CBaseComponent
    {
    public:
        enum state
        {
            registered,     ///< the plugin is constructed but doesn't do anything
            initialized,    ///< the plugin has initialized any state required but is idle
            started,        ///< the plugin is actively running
            stopped         ///< the plugin is no longer running
        };

        virtual ~CBaseComponent() {}

        virtual int GetID() const = 0;

        virtual state GetState() const = 0;

        virtual bool Initialize() = 0;

        virtual bool Startup() = 0;

        virtual bool Shutdown() = 0;
    };
}
