#pragma once
#include "base/base.hpp"
#include "univalue.h"

class CApp : public appbase::IBaseApp
{
public:

    void RelayoutArgs(int& argc, char**& argv);

    void InitOptionMap() override;

    bool Run() override;

private:
    UniValue CallRPC(const std::string &strMethod, const UniValue &params);
};
