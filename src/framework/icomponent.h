#pragma once

namespace appbase
{
    class IComponent
    {
    public:
        enum state
        {
            registered,     ///< the plugin is constructed but doesn't do anything
            initialized,    ///< the plugin has initialized any state required but is idle
            started,        ///< the plugin is actively running
            stopped         ///< the plugin is no longer running
        };

        virtual ~IComponent() {}

        virtual int GetID() const = 0;

        virtual state GetState() const = 0;

        virtual bool Initialize() = 0;

        virtual bool Startup() = 0;

        virtual bool Shutdown() = 0;
    };
}
