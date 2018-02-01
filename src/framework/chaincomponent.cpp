//
// Created by root1 on 18-1-25.
//
#include <iostream>
#include "chaincomponent.hpp"
CChainCommonent::CChainCommonent()
{
}

CChainCommonent::~CChainCommonent()
{
}

bool CChainCommonent::ComponentInitialize()
{
    std::cout << "initialize chain component\n";
    return true;
}

bool CChainCommonent::ComponentStartup()
{
    std::cout << "starting chain component \n";
    return true;
}

bool CChainCommonent::ComponentShutdown()
{
    std::cout << "shutdown chain component \n";
    return true;
}

