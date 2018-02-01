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

    void ComponentInitialize();
    void ComponentStartup();
    void ComponentShutdown();
    const char* whoru(){ return "I am CNetComponent\n";}

};