/*

Copyright (C) 2017-2018, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle Memorial
Institute; the National Renewable Energy Laboratory, operated by the Alliance for Sustainable Energy, LLC; and the
Lawrence Livermore National Laboratory, operated by Lawrence Livermore National Security, LLC.

*/
#ifndef COMMS_BROKER_IMPL_H_
#define COMMS_BROKER_IMPL_H_
#pragma once
#include "BrokerBase.hpp"
#include "CommsBroker.hpp"
#include "CommsInterface.hpp"
#include <atomic>
#include <mutex>
#include <thread>
namespace helics
{
template <class COMMS, class BrokerT>
CommsBroker<COMMS, BrokerT>::CommsBroker () noexcept
{
    static_assert (std::is_base_of<CommsInterface, COMMS>::value, "COMMS object must be a CommsInterface Object");
    static_assert (std::is_base_of<BrokerBase, BrokerT>::value,
                   "Broker must be an object  with a base of BrokerBase");
}

template <class COMMS, class BrokerT>
CommsBroker<COMMS, BrokerT>::CommsBroker (bool arg) noexcept : BrokerT (arg)
{
}

template <class COMMS, class BrokerT>
CommsBroker<COMMS, BrokerT>::CommsBroker (const std::string &obj_name) : BrokerT (obj_name)
{
}
template <class COMMS, class BrokerT>
CommsBroker<COMMS, BrokerT>::~CommsBroker ()
{
    BrokerBase::haltOperations = true;
    std::unique_lock<std::mutex> lock (dataMutex);
    if (comms)
    {
        int exp = 2;
        while (!disconnectionStage.compare_exchange_weak (exp, 3))
        {
            if (exp == 0)
            {
                lock.unlock ();
                brokerDisconnect ();
                lock.lock ();
                exp = 1;
            }
            else
            {
                std::this_thread::sleep_for (std::chrono::milliseconds (50));
            }
        }
    }
    comms = nullptr;  // need to ensure the comms are deleted before the callbacks become invalid
    lock.unlock ();
    BrokerBase::joinAllThreads ();
}

template <class COMMS, class BrokerT>
void CommsBroker<COMMS, BrokerT>::brokerDisconnect ()
{
    std::unique_lock<std::mutex> lock (dataMutex);
    if (comms)
    {
        int exp = 0;
        if (disconnectionStage.compare_exchange_strong (exp, 1))
        {
            auto comm_ptr = comms.get ();
            lock.unlock ();  // we don't want to hold the lock while calling disconnect that could cause deadlock
            comm_ptr->disconnect ();
            disconnectionStage = 2;
        }
    }
    else
    {
        disconnectionStage = 2;
    }
}

template <class COMMS, class BrokerT>
bool CommsBroker<COMMS, BrokerT>::tryReconnect ()
{
    std::unique_lock<std::mutex> lock (dataMutex);
    if (comms)
    {
        auto comm_ptr = comms.get ();
        lock.unlock ();  // we don't want to hold the lock while calling reconnect that could cause deadlock
        return comm_ptr->reconnect ();
    }
    return false;
}

template <class COMMS, class BrokerT>
void CommsBroker<COMMS, BrokerT>::transmit (int route_id, const ActionMessage &cmd)
{
    std::lock_guard<std::mutex> lock (dataMutex);
    if (comms)
    {
        comms->transmit (route_id, cmd);
    }
}

template <class COMMS, class BrokerT>
void CommsBroker<COMMS, BrokerT>::addRoute (int route_id, const std::string &routeInfo)
{
    std::lock_guard<std::mutex> lock (dataMutex);
    if (comms)
    {
        comms->addRoute (route_id, routeInfo);
    }
}

}  // namespace helics
#endif
