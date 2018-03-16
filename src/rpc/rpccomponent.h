#pragma once

#include <memory>
#include "interface/irpccomponent.h"

class CHttpRpcComponent : public IHttpRpcComponent
{
public:
    CHttpRpcComponent();

    ~CHttpRpcComponent();

    bool ComponentInitialize() override;

    bool ComponentStartup() override;

    bool ComponentShutdown() override;

    const char *whoru() const override;




private:

};
