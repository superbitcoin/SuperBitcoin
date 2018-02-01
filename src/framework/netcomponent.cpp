#include <iostream>
#include "netcomponent.hpp"

CNetComponent::CNetComponent()
{
}

CNetComponent::~CNetComponent()
{
}

bool CNetComponent::ComponentInitialize()
{
    return true;
}

bool CNetComponent::ComponentStartup()
{
    std::cout << "starting net component \n";
    return true;
}

bool CNetComponent::ComponentShutdown()
{
    std::cout << "shutdown net component \n";
    return true;
}
