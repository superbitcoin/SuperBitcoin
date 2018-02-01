//
// Created by root1 on 18-1-25.
//
#include <iostream>
#include "chaincomponent.hpp"
#include "netcomponent.hpp"
CChainCommonent::CChainCommonent()
{
}

CChainCommonent::~CChainCommonent()
{
}
void CChainCommonent::ComponentInitialize()
{
    std::cout << "initialize chain component\n";

}
void CChainCommonent::ComponentStartup()
{
    std::cout << "starting chain component \n";
}
void CChainCommonent::ComponentShutdown()
{
    std::cout << "shutdown chain component \n";
}