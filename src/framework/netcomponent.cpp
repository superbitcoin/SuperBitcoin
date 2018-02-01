#include <iostream>
#include "netcomponent.hpp"

CNetComponent::CNetComponent()
{
}

CNetComponent::~CNetComponent()
{
}

void CNetComponent::ComponentInitialize()
{

}
void CNetComponent::ComponentStartup()
{
    std::cout << "starting net component \n";
}
void CNetComponent::ComponentShutdown()
{
    std::cout << "shutdown net component \n";
}