#pragma once

#include "componentid.h"
#include "framework/component.hpp"

class CScheduler;
class CEventManager;
class CClientUIInterface;

class IBaseComponent : public appbase::TComponent<IBaseComponent>
{
public:
    virtual ~IBaseComponent() {}

    enum { ID = CID_BASE };
    virtual int GetID() const override { return ID; }

    virtual bool ComponentInitialize() = 0;
    virtual bool ComponentStartup() = 0;
    virtual bool ComponentShutdown() = 0;
    virtual const char* whoru() const = 0;

    virtual CScheduler* GetScheduler() const = 0;
    virtual CEventManager* GetEventManager() const = 0;
    virtual CClientUIInterface* GetUIInterface() const = 0;

};

#define GET_BASE_INTERFACE(ifObj) \
    auto ifObj = appbase::CBase::Instance().FindComponent<IBaseComponent>()
