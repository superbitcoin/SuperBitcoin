#pragma once
#include "base/base.hpp"

class CApp : public appbase::IBaseApp
{
public:

    void RelayoutArgs(int& argc, char**& argv);

    bool Initialize(int argc, char **argv) override;

    void InitOptionMap() override;

    bool Run() override;
};
