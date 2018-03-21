#pragma once
#include "base/base.hpp"

static const char DEFAULT_RPCCONNECT[] = "127.0.0.1";
static const bool DEFAULT_NAMED = false;
static const int  DEFAULT_HTTP_CLIENT_TIMEOUT = 900;

class CApp : public appbase::IBaseApp
{
public:

    void RelayoutArgs(int& argc, char**& argv);

    void InitOptionMap() override;

    bool Run() override;
};
