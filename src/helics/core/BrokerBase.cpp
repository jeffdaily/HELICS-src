/*

Copyright (C) 2017-2018, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle Memorial
Institute; the National Renewable Energy Laboratory, operated by the Alliance for Sustainable Energy, LLC; and the
Lawrence Livermore National Laboratory, operated by Lawrence Livermore National Security, LLC.

*/

#include "BrokerBase.hpp"

#include "../common/AsioServiceManager.h"
#include "../common/logger.h"
#include "TimeCoordinator.hpp"
#include "helics/helics-config.h"
#include "helicsVersion.hpp"
#include <iostream>
#include <libguarded/guarded.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <boost/uuid/uuid.hpp>  // uuid class
#include <boost/uuid/uuid_generators.hpp>  // generators
#include <boost/uuid/uuid_io.hpp>  // streaming operators etc.
#include "../common/argParser.h"

static inline std::string gen_id ()
{
    boost::uuids::uuid uuid = boost::uuids::random_generator () ();
    std::string uuid_str = boost::lexical_cast<std::string> (uuid);
#ifdef _WIN32
    std::string pid_str = std::to_string (GetCurrentProcessId ());
#else
    std::string pid_str = std::to_string (getpid ());
#endif
    return pid_str + "-" + uuid_str;
}

namespace helics
{
BrokerBase::BrokerBase () noexcept {}

BrokerBase::BrokerBase (const std::string &broker_name) : identifier (broker_name) {}

BrokerBase::~BrokerBase () { joinAllThreads (); }

void BrokerBase::displayHelp ()
{
    std::cout << " Global options for all Brokers:\n";
    variable_map vm;
    const char *const argV[] = {"", "--help"};
    argumentParser(2, argV, vm, {});
}

void BrokerBase::joinAllThreads ()
{
    if (_queue_processing_thread.joinable ())
    {
        actionQueue.push (CMD_TERMINATE_IMMEDIATELY);
        _queue_processing_thread.join ();
    }
}

static const ArgDescriptors extraArgs{
    {"name,n", "name of the broker/core"},
    {"federates", ArgDescriptor::arg_type_t::int_type, "the minimum number of federates that will be connecting"},
    {"minfed", ArgDescriptor::arg_type_t::int_type, "the minimum number of federates that will be connecting"},
    {"maxiter", ArgDescriptor::arg_type_t::int_type, "maximum number of iterations"},
    {"logfile", "the file to log message to"},
    {"loglevel", ArgDescriptor::arg_type_t::int_type, "the level which to log the higher this is set to the more gets logs (-1) for no logging"},
    {"fileloglevel", ArgDescriptor::arg_type_t::int_type, "the level at which messages get sent to the file"},
    {"consoleloglevel", ArgDescriptor::arg_type_t::int_type, "the level at which message get sent to the console"},
    {"minbrokers", ArgDescriptor::arg_type_t::int_type, "the minimum number of core/brokers that need to be connected (ignored in cores)"},
    {"identifier", "name of the core/broker"},
    {"tick", "number of milliseconds per tick counter if there is no broker communication for 2 ticks then secondary actions are taken  (can also be entered as a time like '10s' or '45ms')"},
    {"dumplog",ArgDescriptor::arg_type_t::flag_type,"capture a record of all messages and dump a complete log to file or console on termination"},
    {"timeout", "milliseconds to wait for a broker connection (can also be entered as a time like '10s' or '45ms') "}
};

void BrokerBase::initializeFromCmdArgs (int argc, const char *const *argv)
{
    variable_map vm;
    argumentParser (argc, argv, vm,extraArgs,"min");
    if (vm.count ("min") > 0)
    {
        try
        {
            minFederateCount = std::stod(vm["min"].as<std::string>());
        }
        catch (const std::invalid_argument &ia)
        {
            std::cerr << vm["min"].as<std::string>() << " is not a valid minimum federate count\n";
        }
        
    }
    if (vm.count ("minfed") > 0)
    {
        minFederateCount = vm["minfed"].as<int> ();
    }
    if (vm.count ("federates") > 0)
    {
        minFederateCount = vm["federates"].as<int> ();
    }
    if (vm.count ("minbrokers") > 0)
    {
        minBrokerCount = vm["minbrokers"].as<int> ();
    }
    if (vm.count ("maxiter") > 0)
    {
        maxIterationCount = vm["maxiter"].as<int> ();
    }

    if (vm.count ("name") > 0)
    {
        identifier = vm["name"].as<std::string> ();
    }

    if (vm.count ("dumplog") > 0)
    {
        dumplog = true;
    }
    if (vm.count ("identifier") > 0)
    {
        identifier = vm["identifier"].as<std::string> ();
    }
    if (vm.count ("loglevel") > 0)
    {
        maxLogLevel = vm["loglevel"].as<int> ();
    }
    if (vm.count ("logfile") > 0)
    {
        logFile = vm["logfile"].as<std::string> ();
    }
    if (vm.count ("timeout") > 0)
    {
        auto time_out=loadTimeFromString(vm["timeout"].as<std::string> (),timeUnits::ms);
        timeout = time_out.toCount(timeUnits::ms);
    }
    if (vm.count ("tick") > 0)
    {
        auto time_tick = loadTimeFromString(vm["tick"].as<std::string>(), timeUnits::ms);
        tickTimer = time_tick.toCount(timeUnits::ms);
    }
    if (!noAutomaticID)
    {
        if (identifier.empty ())
        {
            identifier = gen_id ();
        }
    }

    timeCoord = std::make_unique<TimeCoordinator> ();
    timeCoord->setMessageSender ([this](const ActionMessage &msg) { addActionMessage (msg); });

    loggingObj = std::make_unique<Logger> ();
    if (!logFile.empty ())
    {
        loggingObj->openFile (logFile);
    }
    loggingObj->startLogging (maxLogLevel, maxLogLevel);
    _queue_processing_thread = std::thread (&BrokerBase::queueProcessingLoop, this);
}

bool BrokerBase::sendToLogger (Core::federate_id_t federateID,
                               int logLevel,
                               const std::string &name,
                               const std::string &message) const
{
    if ((federateID == 0) || (federateID == global_broker_id))
    {
        if (logLevel > maxLogLevel)
        {
            // check the logging level
            return true;
        }
        if (loggerFunction)
        {
            loggerFunction (logLevel, name, message);
        }
        else if (loggingObj)
        {
            loggingObj->log (logLevel, name + "::" + message);
        }
        return true;
    }
    return false;
}

void BrokerBase::generateNewIdentifier () { identifier = gen_id (); }

void BrokerBase::setLoggerFunction (std::function<void(int, const std::string &, const std::string &)> logFunction)
{
    loggerFunction = std::move (logFunction);
    if (loggerFunction)
    {
        if (loggingObj)
        {
            if (loggingObj->isRunning ())
            {
                loggingObj->haltLogging ();
            }
        }
    }
    else if (!loggingObj->isRunning ())
    {
        loggingObj->startLogging ();
    }
}

void BrokerBase::setLogLevel (int32_t level) { setLogLevels (level, level); }

/** set the logging levels
@param consoleLevel the logging level for the console display
@param fileLevel the logging level for the log file
*/
void BrokerBase::setLogLevels (int32_t consoleLevel, int32_t fileLevel)
{
    consoleLogLevel = consoleLevel;
    fileLogLevel = fileLevel;
    maxLogLevel = std::max (consoleLogLevel, fileLogLevel);
    if (loggingObj)
    {
        loggingObj->changeLevels (consoleLogLevel, fileLogLevel);
    }
}

void BrokerBase::addActionMessage (const ActionMessage &m)
{
    if (isPriorityCommand (m))
    {
        actionQueue.pushPriority (m);
    }
    else
    {
        // just route to the general queue;
        actionQueue.push (m);
    }
}

void BrokerBase::addActionMessage (ActionMessage &&m)
{
    if (isPriorityCommand (m))
    {
        actionQueue.emplacePriority (std::move (m));
    }
    else
    {
        // just route to the general queue;
        actionQueue.emplace (std::move (m));
    }
}

using activeProtector = std::shared_ptr<libguarded::guarded<bool>>;
void timerTickHandler (BrokerBase *bbase, activeProtector active, const boost::system::error_code &error)
{
    auto p = active->lock ();
    if (*p)
    {
        if (error != boost::asio::error::operation_aborted)
        {
            try
            {
                bbase->addActionMessage (CMD_TICK);
            }
            catch (std::exception &e)
            {
                std::cout << "exception caught from addActionMessage" << std::endl;
            }
        }
        else
        {
            ActionMessage M (CMD_TICK);
            setActionFlag (M, error_flag);
            bbase->addActionMessage (M);
        }
    }
}

bool BrokerBase::tryReconnect () { return false; }

void BrokerBase::queueProcessingLoop ()
{
    std::vector<ActionMessage> dumpMessages;
    mainLoopIsRunning.store (true);
    auto serv = AsioServiceManager::getServicePointer ();
    auto serviceLoop = AsioServiceManager::runServiceLoop ();
    boost::asio::steady_timer ticktimer (serv->getBaseService ());
    auto active = std::make_shared<libguarded::guarded<bool>> (true);

    auto timerCallback = [this, active](const boost::system::error_code &ec) {
        timerTickHandler (this, active, ec);
    };
    ticktimer.expires_at (std::chrono::steady_clock::now () + std::chrono::milliseconds (tickTimer));
    ticktimer.async_wait (timerCallback);
    int messagesSinceLastTick = 0;
    auto logDump = [&, this]() {
        if (dumplog)
        {
            for (auto &act : dumpMessages)
            {
                sendToLogger (0, -10, identifier,
                              (boost::format ("|| dl cmd:%s from %d to %d") % prettyPrintString (act) %
                               act.source_id % act.dest_id)
                                .str ());
            }
        }
    };
    while (true)
    {
        auto command = actionQueue.pop ();
        if (dumplog)
        {
            dumpMessages.push_back (command);
        }
        switch (command.action ())
        {
        case CMD_TICK:

            if (messagesSinceLastTick == 0)
            {
                //   std::cout << "sending tick " << std::endl;
                processCommand (std::move (command));
            }
            if (checkActionFlag (command, error_flag))
            {
                serviceLoop = nullptr;
                serviceLoop = AsioServiceManager::runServiceLoop ();
            }
            messagesSinceLastTick = 0;
            // reschedule the timer
            ticktimer.expires_at (std::chrono::steady_clock::now () + std::chrono::milliseconds (tickTimer));
            ticktimer.async_wait (timerCallback);
            break;
        case CMD_IGNORE:
            break;
        case CMD_TERMINATE_IMMEDIATELY:
            ticktimer.cancel ();
            serviceLoop = nullptr;
            mainLoopIsRunning.store (false);
            active->store (false);
            logDump ();
            return;  // immediate return
        case CMD_STOP:
            ticktimer.cancel ();
            serviceLoop = nullptr;
            if (!haltOperations)
            {
                processCommand (std::move (command));
                mainLoopIsRunning.store (false);
                active->store (false);
                logDump ();
                return processDisconnect ();
            }

            return;
        default:
            if (!haltOperations)
            {
                ++messagesSinceLastTick;
                if (isPriorityCommand (command))
                {
                    processPriorityCommand (std::move (command));
                }
                else
                {
                    processCommand (std::move (command));
                }
            }
        }
    }
}
}  // namespace helics
