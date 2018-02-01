#pragma once
//
// Created by root1 on 18-1-25.
//
#include "../interface/ichaincomponent.h"

using namespace appbase;
struct database { };

class CChainCommonent : public IChainComponent
{
public:
    CChainCommonent();
    ~CChainCommonent();
    bool ComponentInitialize() override;
    bool ComponentStartup() override;
    bool ComponentShutdown() override;

    database& db() { return _db; }
    const char* whoru() const override { return "I am CChainCommonent\n";}

private:
    database _db;
};
