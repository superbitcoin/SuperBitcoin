//
// Created by root1 on 18-1-25.
//
#include "chaincomponent.hpp"
#include "netcomponent.hpp"
CChainCommonent::CChainCommonent()
{
}

CChainCommonent::~CChainCommonent()
{
}
void CChainCommonent::ComponentInitialize( const variables_map& options )
{
    std::cout << "initialize chain component\n";
    CNetComponent& chain = (CNetComponent&)CBase::Instance().GetComponent("CNetComponent");
    std::cout << chain.whoru();
}
void CChainCommonent::ComponentStartup()
{
    std::cout << "starting chain component \n";
}
void CChainCommonent::ComponentShutdown()
{
    std::cout << "shutdown chain component \n";
}