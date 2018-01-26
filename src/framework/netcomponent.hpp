/*************************************************
 * File name:		// basecomponent.hpp
 * Author:
 * Date: 		    //2018.01.26
 * Description:		// This is an example of the implementation of a specific component

 * Others:		    //
 * History:		    // 2018.01.26

 * 1. Date:
 * Author:
 * Modification:
*************************************************/
#pragma once

#include "component.hpp"

using namespace appbase;
class CNetComponent : public CComponent<CNetComponent>
{
public:
    CNetComponent();
    ~CNetComponent();

    virtual void SetProgramOptions( options_description& cli, options_description& cfg ) override
    {
        cfg.add_options()
                ("listen-endpoint", boost::program_options::value<string>()->default_value( "127.0.0.1:9876" ), "The local IP address and port to listen for incoming connections.")
                ("remote-endpoint", boost::program_options::value< vector<string> >()->composing(), "The IP address and port of a remote peer to sync with.")
                ("public-endpoint", boost::program_options::value<string>()->default_value( "0.0.0.0:9876" ), "The public IP address and port that should be advertized to peers.")
                ;
    };
    void ComponentInitialize( const variables_map& options );
    void ComponentStartup();
    void ComponentShutdown();
    const char* whoru(){ return "I am CChainCommonent\n";}

};