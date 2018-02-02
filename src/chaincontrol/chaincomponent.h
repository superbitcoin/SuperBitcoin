#pragma once

#include "../interface/ichaincomponent.h"

struct database
{
};

class CChainCommonent : public IChainComponent
{
public:
    CChainCommonent();

    ~CChainCommonent();

    bool ComponentInitialize() override;

    bool ComponentStartup() override;

    bool ComponentShutdown() override;

    database &db()
    {
        return _db;
    }

    const char *whoru() const override
    {
        return "I am CChainCommonent\n";
    }

private:
    database _db;
};
