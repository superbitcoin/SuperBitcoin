// Copyright (c) 2015-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zmqnotificationinterface.h"
#include "zmqpublishnotifier.h"

#include "framework/version.h"
#include "block/validation.h"
#include "sbtccore/streams.h"
#include "utils/util.h"

void zmqError(const char *str)
{
    //    NLogFormat("zmq: Error: %s, errno=%s\n", str, zmq_strerror(errno));
}

CZMQNotificationInterface::CZMQNotificationInterface() : pcontext(nullptr)
{
}

CZMQNotificationInterface::~CZMQNotificationInterface()
{
    Shutdown();

    for (std::list<CZMQAbstractNotifier *>::iterator i = notifiers.begin(); i != notifiers.end(); ++i)
    {
        delete *i;
    }
}

CZMQNotificationInterface *CZMQNotificationInterface::Create()
{
    CZMQNotificationInterface *notificationInterface = nullptr;
    std::map<std::string, CZMQNotifierFactory> factories;
    std::list<CZMQAbstractNotifier *> notifiers;

    factories["pubhashblock"] = CZMQAbstractNotifier::Create<CZMQPublishHashBlockNotifier>;
    factories["pubhashtx"] = CZMQAbstractNotifier::Create<CZMQPublishHashTransactionNotifier>;
    factories["pubrawblock"] = CZMQAbstractNotifier::Create<CZMQPublishRawBlockNotifier>;
    factories["pubrawtx"] = CZMQAbstractNotifier::Create<CZMQPublishRawTransactionNotifier>;

    for (std::map<std::string, CZMQNotifierFactory>::const_iterator i = factories.begin(); i != factories.end(); ++i)
    {
        std::string arg("-zmq" + i->first);
        if (gArgs.IsArgSet(arg))
        {
            CZMQNotifierFactory factory = i->second;
            std::string address = gArgs.GetArg(arg, std::string(""));
            CZMQAbstractNotifier *notifier = factory();
            notifier->SetType(i->first);
            notifier->SetAddress(address);
            notifiers.push_back(notifier);
        }
    }

    if (!notifiers.empty())
    {
        notificationInterface = new CZMQNotificationInterface();
        notificationInterface->notifiers = notifiers;

        if (!notificationInterface->Initialize())
        {
            delete notificationInterface;
            notificationInterface = nullptr;
        }
    }

    return notificationInterface;
}

// Called at startup to conditionally set up ZMQ socket(s)
bool CZMQNotificationInterface::Initialize()
{
    NLogFormat("zmq: Initialize notification interface\n");
    assert(!pcontext);

    pcontext = zmq_init(1);

    if (!pcontext)
    {
        zmqError("Unable to initialize context");
        return false;
    }

    std::list<CZMQAbstractNotifier *>::iterator i = notifiers.begin();
    for (; i != notifiers.end(); ++i)
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->Initialize(pcontext))
        {
            NLogFormat("  Notifier %s ready (address = %s)\n", notifier->GetType(), notifier->GetAddress());
        } else
        {
            NLogFormat("  Notifier %s failed (address = %s)\n", notifier->GetType(), notifier->GetAddress());
            break;
        }
    }

    if (i != notifiers.end())
    {
        return false;
    }

    return true;
}

// Called during shutdown sequence
void CZMQNotificationInterface::Shutdown()
{
    NLogFormat("zmq: Shutdown notification interface\n");
    if (pcontext)
    {
        for (std::list<CZMQAbstractNotifier *>::iterator i = notifiers.begin(); i != notifiers.end(); ++i)
        {
            CZMQAbstractNotifier *notifier = *i;
            NLogFormat("   Shutdown notifier %s at %s\n", notifier->GetType(), notifier->GetAddress());
            notifier->Shutdown();
        }
        zmq_ctx_destroy(pcontext);

        pcontext = 0;
    }
}

void CZMQNotificationInterface::UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork,
                                                bool fInitialDownload)
{
    if (fInitialDownload || pindexNew == pindexFork) // In IBD or blocks were disconnected without any new ones
        return;

    for (std::list<CZMQAbstractNotifier *>::iterator i = notifiers.begin(); i != notifiers.end();)
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyBlock(pindexNew))
        {
            i++;
        } else
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
        }
    }
}

void CZMQNotificationInterface::TransactionAddedToMempool(const CTransactionRef &ptx)
{
    // Used by BlockConnected and BlockDisconnected as well, because they're
    // all the same external callback.
    const CTransaction &tx = *ptx;

    for (std::list<CZMQAbstractNotifier *>::iterator i = notifiers.begin(); i != notifiers.end();)
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyTransaction(tx))
        {
            i++;
        } else
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
        }
    }
}

void CZMQNotificationInterface::BlockConnected(const std::shared_ptr<const CBlock> &pblock,
                                               const CBlockIndex *pindexConnected,
                                               const std::vector<CTransactionRef> &vtxConflicted)
{
    for (const CTransactionRef &ptx : pblock->vtx)
    {
        // Do a normal notify for each transaction added in the block
        TransactionAddedToMempool(ptx);
    }
}

void CZMQNotificationInterface::BlockDisconnected(const std::shared_ptr<const CBlock> &pblock)
{
    for (const CTransactionRef &ptx : pblock->vtx)
    {
        // Do a normal notify for each transaction removed in block disconnection
        TransactionAddedToMempool(ptx);
    }
}
