#pragma once
//
// Created by root1 on 18-1-25.
//
#include "component.hpp"

using namespace appbase;
struct database { };

class CChainCommonent : public CComponent<CChainCommonent>
{
public:
    CChainCommonent();
    ~CChainCommonent();
    void ComponentInitialize();
    void ComponentStartup() ;
    void ComponentShutdown() ;

    database& db() { return _db; }
    const char* whoru(){ return "I am CChainCommonent\n";}

private:
    database _db;
};
