#include <iostream>
#include "netcomponent.h"

CNetComponent::CNetComponent()
{
}

CNetComponent::~CNetComponent()
{
}

bool CNetComponent::ComponentInitialize()
{
    std::cout << "initialize net component \n";
    return true;
}

bool CNetComponent::ComponentStartup()
{
    std::cout << "startup net component \n";
    return true;
}

bool CNetComponent::ComponentShutdown()
{
    std::cout << "shutdown net component \n";
    return true;
}
