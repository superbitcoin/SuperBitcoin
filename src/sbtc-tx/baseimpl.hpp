#pragma once
#include "base/base.hpp"

class CApp : public appbase::IBaseApp
{
public:
    bool Run(int argc, char *argv[]);

    //bool Initialize(int argc, char **argv) override;

protected:
    void InitOptionMap() override;
};
