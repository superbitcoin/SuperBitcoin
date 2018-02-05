#pragma once

#include <string>
#include <vector>
#include "sbtccore/streams.h"
#include "framework/base.hpp"
#include "interface/inetcomponent.h"

template<typename... TArgs>
bool SendNetMessage(int64_t nodeID, const std::string& command, int version, int flags, TArgs&& ... args)
{
    std::vector<unsigned char> msgData;
    {
        CVectorWriter{SER_NETWORK, version | flags, msgData, 0, std::forward<TArgs>(args)...};
    }
    GET_NET_INTERFACE(ifNetObj);
    return ifNetObj->SendNetMessage(nodeID, command, msgData);
}

