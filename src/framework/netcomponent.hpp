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

#include "../interface/inetcomponent.h"

using namespace appbase;
class CNetComponent : public INetComponent
{
public:
    CNetComponent();
    ~CNetComponent();

    bool ComponentInitialize() override;
    bool ComponentStartup() override;
    bool ComponentShutdown() override;
    const char* whoru() const override { return "I am CNetComponent\n";}

};