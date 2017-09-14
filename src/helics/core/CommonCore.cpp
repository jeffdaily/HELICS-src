/*

Copyright (C) 2017, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle Memorial
Institute; the National Renewable Energy Laboratory, operated by the Alliance for Sustainable Energy, LLC; and the
Lawrence Livermore National Laboratory, operated by Lawrence Livermore National Security, LLC.

*/
#include "CommonCore.h"
#include "ActionMessage.h"
#include "BasicHandleInfo.h"
#include "EndpointInfo.h"
#include "FederateState.h"
#include "FilterInfo.h"
#include "PublicationInfo.h"
#include "SubscriptionInfo.h"
#include <boost/filesystem.hpp>

#include "helics/common/stringToCmdLine.h"

#include "CoreFactory.h"
#include "FilterFunctions.h"
#include "helics/core/core-exceptions.h"

#include <algorithm>
#include <boost/program_options.hpp>
#include <cassert>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>

#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid.hpp>  // uuid class
#include <boost/uuid/uuid_generators.hpp>  // generators
#include <boost/uuid/uuid_io.hpp>  // streaming operators etc.

#define USE_LOGGING 1
#if USE_LOGGING
#if HELICS_HAVE_GLOG
#include <glog/logging.h>
#define ENDL ""
#else
#define LOG(LEVEL) std::cout 
#define ENDL std::endl
#endif
#else
#define LOG(LEVEL) std::ostringstream ()
#define ENDL std::endl
#endif

static inline std::string gen_id ()
{
    boost::uuids::uuid uuid = boost::uuids::random_generator () ();
    std::string uuid_str = boost::lexical_cast<std::string> (uuid);
#ifdef _WIN32
    std::string pid_str = boost::lexical_cast<std::string> (GetCurrentProcessId ());
#else
    std::string pid_str = boost::lexical_cast<std::string> (getpid ());
#endif
    return pid_str + "-" + uuid_str;
}

namespace helics
{
using federate_id_t = Core::federate_id_t;
using Handle = Core::Handle;


static void argumentParser (int argc, char *argv[], boost::program_options::variables_map &vm_map);

CommonCore::CommonCore () noexcept {}

CommonCore::CommonCore (const std::string &core_name) : identifier (core_name) {}

void CommonCore::initialize (const std::string &initializationString)
{
    if ((coreState == created))  // don't do the compare exchange here since we do that in the initialize fromArgs
    {  // and we can tolerate a spurious call
        stringToCmdLine cmdline (initializationString);
        initializeFromArgs (cmdline.getArgCount (), cmdline.getArgV ());
    }
}

void CommonCore::initializeFromArgs (int argC, char *argv[])
{
    namespace po = boost::program_options;
    core_state_t exp = created;
    if (coreState.compare_exchange_strong (exp, core_state_t::initialized))
    {
        po::variables_map vm;
        argumentParser (argC, argv, vm);
        if (vm.count ("min") > 0)
        {
            _min_federates = vm["min"].as<int> ();
        }
		if (vm.count("minfed") > 0)
		{
			_min_federates = vm["minfed"].as<int>();
		}
        if (vm.count ("federates") > 0)
        {
            _min_federates = vm["federates"].as<int> ();
        }

        if (vm.count ("maxiter") > 0)
        {
            _max_iterations = vm["maxiter"].as<int> ();
        }

        if (vm.count ("name") > 0)
        {
            identifier = vm["name"].as<std::string> ();
        }

        if (vm.count ("identifier") > 0)
        {
            identifier = vm["identifier"].as<std::string> ();
        }

        if (identifier.empty ())
        {
            identifier = gen_id ();
        }
        _queue_processing_thread = std::thread (&CommonCore::queueProcessingLoop, this);
    }
}

bool CommonCore::connect ()
{
    if (coreState >= core_state_t::initialized)
    {
        core_state_t exp = core_state_t::initialized;
        if (coreState.compare_exchange_strong (exp, core_state_t::connecting))
        {
            bool res = brokerConnect ();
            if (res)
            {
                // now register this core object as a broker
				
                ActionMessage m (CMD_REG_BROKER);
                m.name = getIdentifier ();
                m.info ().target = getAddress ();
                transmit (0, m);
				coreState = core_state_t::connected;
            }
            else
            {
                coreState = core_state_t::initialized;
            }
            return res;
        }
		if (coreState == core_state_t::connecting)
		{
			while (coreState == core_state_t::connecting)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
    }
    return isConnected();
}


bool CommonCore::isConnected () const { return ((coreState == operating) || (coreState == connected)); }

void CommonCore::disconnect ()
{
    brokerDisconnect ();
    coreState = terminated;
    /*We need to enrure that the destructor is not called immediately upon calling unregister
    otherwise this would be a mess and probably cause seg faults so we capture it in a local variable
    that will be destroyed on function exit
    */
    auto keepCoreAlive = CoreFactory::findCore (identifier);
    if (keepCoreAlive)
    {
		if (keepCoreAlive.get() == this)
		{
			CoreFactory::unregisterCore(identifier);
		}
       
    }

    if (!prevIdentifier.empty ())
    {
        auto keepCoreAlive2 = CoreFactory::findCore (prevIdentifier);
        if (keepCoreAlive2)
        {
			if (keepCoreAlive2.get() == this)
			{
				CoreFactory::unregisterCore(prevIdentifier);
			}
        }
    }
}

void argumentParser (int argc, char *argv[], boost::program_options::variables_map &vm_map)
{
    namespace po = boost::program_options;
    po::options_description cmd_only ("command line only");
    po::options_description config ("configuration");
    po::options_description hidden ("hidden");

    // clang-format off
	// input boost controls
	cmd_only.add_options()
		("help,h", "produce help message")
		("version,v", "helics version number")
		("config-file", po::value<std::string>(), "specify a configuration file to use");


	config.add_options()
		("name,n", po::value<std::string>(), "name of the core")
		("federates", po::value<int>(), "the minimum number of federates that will be connecting")
		("minfed",po::value<int>(),"the minimum number of federates that will be connecting")
		("maxiter",po::value<int>(),"maximum number of iterations")
		("logfile",po::value<std::string>(),"the file to log message to")
		("loglevel",po::value<int>(),"the level which to log the higher this is set to the more gets logs (-1) for no logging")
		("fileloglevel",po::value<int>(),"the level at which messages get sent to the file")
		("consoleloglevel",po::value<int>(),"the level at which message get sent to the console")
		("identifier", po::value<std::string>(), "name of the core");
		

	hidden.add_options() ("min", po::value<int>(), "minimum number of federates");

    // clang-format on

    po::options_description cmd_line ("command line options");
    po::options_description config_file ("configuration file options");
    po::options_description visible ("allowed options");

    cmd_line.add (cmd_only).add (config).add (hidden);
    config_file.add (config);
    visible.add (cmd_only).add (config);

    po::positional_options_description p;
    p.add ("min", -1);

    po::variables_map cmd_vm;
    try
    {
        po::store (
          po::command_line_parser (argc, argv).options (cmd_line).positional (p).allow_unregistered ().run (),
          cmd_vm);
    }
    catch (std::exception &e)
    {
        std::cerr << e.what () << std::endl;
        throw (e);
    }

    po::notify (cmd_vm);

    // objects/pointers/variables/constants


    // program options control
    if (cmd_vm.count ("help") > 0)
    {
        std::cout << visible << '\n';
        return;
    }

    if (cmd_vm.count ("version") > 0)
    {
        std::cout << HELICS_VERSION_MAJOR << '.' << HELICS_VERSION_MINOR << '.' << HELICS_VERSION_PATCH << " (" << HELICS_DATE << ")\n";
        return;
    }


    po::store (po::command_line_parser (argc, argv).options (cmd_line).positional (p).allow_unregistered ().run (),
               vm_map);

    if (cmd_vm.count ("config-file") > 0)
    {
        std::string config_file_name = cmd_vm["config-file"].as<std::string> ();
        if (!boost::filesystem::exists (config_file_name))
        {
            std::cerr << "config file " << config_file_name << " does not exist\n";
            throw (std::invalid_argument ("unknown config file"));
        }
        else
        {
            std::ifstream fstr (config_file_name.c_str ());
            po::store (po::parse_config_file (fstr, config_file), vm_map);
            fstr.close ();
        }
    }

    po::notify (vm_map);
}

CommonCore::~CommonCore ()
{
    if (_queue_processing_thread.joinable ())
    {
        _queue.push (CMD_TERMINATE_IMMEDIATELY);
        _queue_processing_thread.join ();
    }
}

FederateState *CommonCore::getFederate (federate_id_t federateID) const
{
    // only activate the lock if we not in an operating state
    auto lock = (coreState == operating) ? std::unique_lock<std::mutex> (_mutex, std::defer_lock) :
                                           std::unique_lock<std::mutex> (_mutex);

    if (isValidIndex (federateID, _federates))
    {
        return _federates[federateID].get ();
    }
    else
    {
        auto fnd = global_id_translation.find (federateID);
        if (fnd != global_id_translation.end ())
        {
            return _federates[fnd->second].get ();
        }
    }

    return nullptr;
}

FederateState *CommonCore::getHandleFederate (Handle id_)
{
    // only activate the lock if we not in an operating state
    auto lock = (coreState == operating) ? std::unique_lock<std::mutex> (_handlemutex, std::defer_lock) :
                                           std::unique_lock<std::mutex> (_handlemutex);
    // this list is now constant no need to lock
    if (isValidIndex (id_, handles))
    {  // now need to be careful about deadlock here
        auto lock2 = (coreState == operating) ? std::unique_lock<std::mutex> (_mutex, std::defer_lock) :
                                                std::unique_lock<std::mutex> (_mutex);
        return _federates[handles[id_]->local_fed_id].get ();
    }

    return nullptr;
}

BasicHandleInfo *CommonCore::getHandleInfo (Handle id_) const
{
    // only activate the lock if we not in an operating state
    auto lock = (coreState == operating) ? std::unique_lock<std::mutex> (_handlemutex, std::defer_lock) :
                                           std::unique_lock<std::mutex> (_handlemutex);
    if (isValidIndex (id_, handles))
    {
        return handles[id_].get ();
    }

    return nullptr;
}


BasicHandleInfo *CommonCore::getLocalEndpoint (const std::string &name)
{
    // only activate the lock if we not in an operating state
    auto lock = (coreState == operating) ? std::unique_lock<std::mutex> (_handlemutex, std::defer_lock) :
                                           std::unique_lock<std::mutex> (_handlemutex);
    auto fnd = endpoints.find (name);
    if (fnd != endpoints.end ())
    {
        return getHandleInfo (fnd->second);
    }
    return nullptr;
}


bool CommonCore::isLocal (Core::federate_id_t global_id) const
{
    // only activate the lock if we not in an operating state
    auto lock = (coreState == operating) ? std::unique_lock<std::mutex> (_mutex, std::defer_lock) :
                                           std::unique_lock<std::mutex> (_mutex);
    auto fnd = global_id_translation.find (global_id);
    return (fnd != global_id_translation.end ());
}

int32_t CommonCore::getRoute (Core::federate_id_t global_id) const
{
    // only activate the lock if we not in an operating state
    auto lock = (coreState == operating) ? std::unique_lock<std::mutex> (_mutex, std::defer_lock) :
                                           std::unique_lock<std::mutex> (_mutex);
    auto fnd = routing_table.find (global_id);
    return (fnd != routing_table.end ()) ? fnd->second : 0;
}

bool CommonCore::isInitialized () const { return (coreState >= initialized); }

bool CommonCore::isJoinable () const { return ((coreState != created) && (coreState < operating)); }
void CommonCore::error (federate_id_t federateID, int errorCode)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    ActionMessage m (CMD_ERROR);
    m.source_id = fed->global_id;
    m.source_handle = errorCode;
    addCommand(m);
    fed->addAction (m);
    convergence_state ret = convergence_state::complete;
    while (ret != convergence_state::error)
    {
        ret = fed->genericUnspecifiedQueueProcess ();
        if (ret == convergence_state::halted)
        {
            break;
        }
    }
}


void CommonCore::finalize (federate_id_t federateID)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    ActionMessage bye (CMD_DISCONNECT);
    bye.source_id = fed->global_id;

	fed->addAction(bye);
	convergence_state ret = convergence_state::complete;
	while (ret != convergence_state::halted)
	{
		ret = fed->genericUnspecifiedQueueProcess();
		if (ret == convergence_state::error)
		{
			break;
		}
	}
    addCommand(bye);
	
}

bool CommonCore::allInitReady () const
{
    std::lock_guard<std::mutex> lock (_mutex);
    // the federate count must be greater than the min size
    if (static_cast<decltype (_min_federates)> (_federates.size ()) < _min_federates)
    {
        return false;
    }
    // all federates must be requesting init
    for (auto &fed : _federates)
    {
        if (fed->init_transmitted == false)
        {
            return false;
        }
    }
    return true;
}


bool CommonCore::allDisconnected () const
{
    // all federates must have hit finished state
    for (auto &fed : _federates)
    {
        if ((fed->getState () == HELICS_FINISHED)|| (fed->getState() == HELICS_ERROR))
        {
            continue;
        }
		return false;
    }
    return true;
}

void CommonCore::enterInitializingState (federate_id_t federateID)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    switch (fed->getState ())
    {
    case HELICS_CREATED:
        break;
    case HELICS_INITIALIZING:
        return;
    default:
        throw (invalidFunctionCall ());
    }

    bool exp = false;
    if (fed->init_requested.compare_exchange_strong (exp, true))
    {  // only enter this loop once per federate
        ActionMessage m (CMD_INIT);
        m.source_id = fed->global_id;
        addCommand(m);

        auto check = fed->enterInitState ();
        if (check != convergence_state::complete)
        {
            fed->init_requested = false;
            throw (functionExecutionFailure ());
        }
        return;
    }
    throw (invalidFunctionCall ());
}


convergence_state CommonCore::enterExecutingState (federate_id_t federateID, convergence_state converged)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    if (HELICS_EXECUTING == fed->getState ())
    {
        return convergence_state::complete;
    }
    if (HELICS_INITIALIZING != fed->getState ())
    {
        throw (invalidFunctionCall ());
    }
    // do an exec check on the fed to process previously received messages so it can't get in a deadlocked state
    ActionMessage exec (CMD_EXEC_CHECK);
    fed->addAction (exec);
    // TODO:: check for error conditions?
    return fed->enterExecutingState (converged);
}


federate_id_t CommonCore::registerFederate (const std::string &name, const CoreFederateInfo &info)
{
    if (coreState == created)
    {
        throw (invalidFunctionCall ("Core has not been initialized yet"));
    }
    if (coreState >= operating)
    {
        throw (invalidFunctionCall ("Core has already moved to operating state"));
    }
    auto fed = std::make_unique<FederateState> (name, info);

    std::unique_lock<std::mutex> lock (_mutex);
    auto id = fed->local_id = static_cast<decltype (fed->local_id)> (_federates.size ());

    fed->setParent (this);
    _federates.push_back (std::move (fed));
    federateNames.emplace (name, id);
    lock.unlock ();

    ActionMessage m (CMD_REG_FED);
    m.name = name;
    if (global_broker_id != 0)
    {
        m.source_id = global_broker_id;

        transmit (0, m);  // just directly transmit, no need to process in the queue since it is a priority message
    }
    else
    {
        // this will get processed when this core is assigned a global id
        delayTransmitQueue.push (m);
    }


    // now wait for the federateQueue to get the response
    auto valid = getFederate (id)->waitSetup ();
    if (valid == convergence_state::complete)
    {
        return id;
    }
    throw (registrationFailure ());
}


const std::string &CommonCore::getFederateName (federate_id_t federateID) const
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    return fed->getIdentifier();
}


federate_id_t CommonCore::getFederateId (const std::string &name)
{
    std::lock_guard<std::mutex> lock (_mutex);

    auto res = federateNames.find (name);
    if (res != federateNames.end ())
    {
        return res->second;
    }

    return invalid_fed_id;
}


int32_t CommonCore::getFederationSize ()
{
    if (coreState >= operating)
    {
        return _global_federation_size;
    }
    // if we are in initialization return the local federation size
    std::lock_guard<std::mutex> lock (_mutex);
    return static_cast<int32_t> (_federates.size ());
}


Time CommonCore::timeRequest (federate_id_t federateID, Time next)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    if (HELICS_EXECUTING == fed->getState ())
    {
        auto ret = fed->requestTime (next, convergence_state::complete);
        return ret.stepTime;
    }
    throw (invalidFunctionCall ());
}


iterationTime CommonCore::requestTimeIterative (federate_id_t federateID, Time next, convergence_state converged)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    switch (converged)
    {
    case convergence_state::halted:
        finalize (federateID);
        return {fed->grantedTime (), converged};
    case convergence_state::error:
        error (federateID);
        return {fed->grantedTime (), converged};
    default:
        if (HELICS_EXECUTING != fed->getState ())
        {
            throw (invalidFunctionCall ());
        }
    }

    // limit the iterations
    if (converged != convergence_state::complete)
    {
        if (fed->getCurrentIteration () >= _max_iterations)
        {
            converged = convergence_state::complete;
        }
    }

    return fed->requestTime (next, converged);
}


uint64_t CommonCore::getCurrentReiteration (federate_id_t federateID)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw invalidIdentifier ("federateID not valid");
    }
    return fed->getCurrentIteration ();
}


void CommonCore::setMaximumIterations (federate_id_t federateID, uint64_t iterations)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }

    auto info = fed->getInfo ();
    info.max_iterations = iterations;
    fed->UpdateFederateInfo (info);
}


void CommonCore::setTimeDelta (federate_id_t federateID, Time time)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    if (time < timeZero)
    {
        throw (invalidParameter ("timeDelta must be greater than or equal to zero"));
    }
    // time delta should not be zero but we allow it here for convenience
    if (time == timeZero)
    {
        time = timeEpsilon;
    }
    auto info = fed->getInfo ();
    info.timeDelta = time;
    fed->UpdateFederateInfo (info);
}


void CommonCore::setLookAhead (federate_id_t federateID, Time lookAheadTime)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    if (lookAheadTime < timeZero)
    {
        throw (invalidParameter ());
    }
    auto info = fed->getInfo ();
    info.lookAhead = lookAheadTime;
    fed->UpdateFederateInfo (info);
}

void CommonCore::setImpactWindow (federate_id_t federateID, Time impactTime)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }

    if (impactTime < timeZero)
    {
        throw (invalidParameter ());
    }
    auto info = fed->getInfo ();
    info.impactWindow = impactTime;
    fed->UpdateFederateInfo (info);
}


Core::Handle CommonCore::getNewHandle () { return handleCounter++; }

// comparison auto lambda  Functions like a template
static auto compareFunc = [](const auto &A, const auto &B) { return (A->id < B->id); };

BasicHandleInfo *CommonCore::createBasicHandle (Handle id_,
                                                federate_id_t global_federateId,
                                                federate_id_t local_federateId,
                                                BasicHandleType HandleType,
                                                const std::string &key,
                                                const std::string &type,
                                                const std::string &units,
                                                bool required)
{
    auto hndl = std::make_unique<BasicHandleInfo> (id_, global_federateId, HandleType, key, type, units, required);
    hndl->local_fed_id = local_federateId;
    std::lock_guard<std::mutex> lock (_handlemutex);

    // may need to resize the handles
    if (static_cast<Handle> (handles.size ()) <= id_)
    {
        handles.resize (id_ + 5);
    }
    auto infoPtr = hndl.get ();
    handles[id_] = std::move (hndl);
    return infoPtr;
}

BasicHandleInfo *CommonCore::createBasicHandle(Handle id_,
	federate_id_t global_federateId,
	federate_id_t local_federateId,
	BasicHandleType HandleType,
	const std::string &key,
	const std::string &target,
	const std::string &type_in,
	const std::string &type_out)
{
	auto hndl = std::make_unique<BasicHandleInfo>(id_, global_federateId, HandleType, key,target, type_in, type_out);
	hndl->local_fed_id = local_federateId;
	std::lock_guard<std::mutex> lock(_handlemutex);

	// may need to resize the handles
	if (static_cast<Handle> (handles.size()) <= id_)
	{
		handles.resize(id_ + 5);
	}
	auto infoPtr = hndl.get();
	handles[id_] = std::move(hndl);
	return infoPtr;
}

Handle CommonCore::registerSubscription (federate_id_t federateID,
                                         const std::string &key,
                                         const std::string &type,
                                         const std::string &units,
                                         handle_check_mode check_mode)
{
    LOG (INFO) << "registering SUB " << key << ENDL;

    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    if (fed->getState () != HELICS_CREATED)
    {
        throw (invalidFunctionCall ());
    }

    auto id = getNewHandle ();
    fed->createSubscription (id, key, type, units, check_mode);

    createBasicHandle (id, fed->global_id, fed->local_id, HANDLE_SUB, key, type, units,
                       (check_mode == handle_check_mode::required));


    ActionMessage m (CMD_REG_SUB);
    m.source_id = fed->global_id;
    m.source_handle = id;
    m.name = key;
    m.info ().type = type;
    m.info ().units = units;
    m.required = (check_mode == handle_check_mode::required);

    std::unique_lock<std::mutex> lock (_handlemutex);
    auto fndpub = publications.find (key);
    if (fndpub != publications.end ())
    {
        auto pubhandle = fndpub->second;
        auto pubid = handles[pubhandle]->fed_id;
        lock.unlock ();
        m.processingComplete = true;
        // send to broker and core
        addCommand (m);
        // now send the same command to the publication
        m.dest_handle = pubhandle;
        m.dest_id = pubid;
        // send to
        addCommand (m);
        // now send the notification to the subscription
        ActionMessage notice (CMD_NOTIFY_PUB);
        notice.dest_id = fed->global_id;
        notice.dest_handle = id;
        notice.source_id = pubid;
        notice.source_handle = pubhandle;
        fed->addAction (notice);
    }
    else
    {
        lock.unlock ();
        // we didn't find it so just pass it on to the broker
        addCommand (m);
    }

    return id;
}


Handle CommonCore::getSubscription (federate_id_t federateID, const std::string &key)
{
    auto fed = getFederate (federateID);
    if (fed != nullptr)
    {
        return fed->getSubscription (key)->id;
    }
    return invalid_Handle;
}


Handle CommonCore::registerPublication (federate_id_t federateID,
                                        const std::string &key,
                                        const std::string &type,
                                        const std::string &units)
{
    LOG (INFO) << "registering PUB " << key << ENDL;


    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    if (fed->getState () != HELICS_CREATED)
    {
        throw (invalidFunctionCall ());
    }
    std::unique_lock<std::mutex> lock (_handlemutex);
    auto fnd = publications.find (key);
    if (fnd != publications.end ())  // this key is already found
    {
        throw (invalidParameter ());
    }
    auto id = getNewHandle ();
    publications.emplace (key, id);
    lock.unlock ();
    fed->createPublication (id, key, type, units);

    createBasicHandle (id, fed->global_id, fed->local_id, HANDLE_PUB, key, type, units, false);

    ActionMessage m (CMD_REG_PUB);
    m.source_id = fed->global_id;
    m.source_handle = id;
    m.name = key;
    m.info ().type = type;
    m.info ().units = units;

    _queue.push (m);
    return id;
}


Handle CommonCore::getPublication (federate_id_t federateID, const std::string &key)
{
    auto fed = getFederate (federateID);
    if (fed != nullptr)
    {
        auto pub = fed->getPublication (key);
        if (pub != nullptr)
        {
            return pub->id;
        }
    }
    return invalid_Handle;
}

const std::string nullStr;

const std::string &CommonCore::getUnits (Handle handle) const
{
    auto handleInfo = getHandleInfo (handle);
    if (handleInfo != nullptr)
    {
        return handleInfo->units;
    }
    return nullStr;
}


const std::string &CommonCore::getType (Handle handle) const
{
    auto handleInfo = getHandleInfo (handle);
    if (handleInfo != nullptr)
    {
        return handleInfo->units;
    }
    return nullStr;
}


void CommonCore::setValue (Handle handle_, const char *data, uint64_t len)
{
    auto handleInfo = getHandleInfo (handle_);
    if (handleInfo == nullptr)
    {
        throw (invalidIdentifier ("Handle not valid"));
    }
    if (handleInfo->what != HANDLE_PUB)
    {
        throw (invalidIdentifier ("handle does not point to a publication"));
    }


    if (!handleInfo->used)
    {
        return;  // if the value is not required do nothing
    }
    LOG (INFO) << "setValue: '" << std::string (data, len) << "'" << ENDL;
    ActionMessage mv (CMD_PUB);
    mv.source_id = handleInfo->fed_id;
    mv.source_handle = handle_;
    mv.payload = std::string (data, len);

    _queue.push (mv);
}


std::shared_ptr<const data_block> CommonCore::getValue (Handle handle_)
{
    auto handleInfo = getHandleInfo (handle_);
    if (handleInfo == nullptr)
    {
        throw (invalidIdentifier ("Handle is invalid"));
    }
    if (handleInfo->what != HANDLE_SUB)
    {
        throw (invalidIdentifier ("Handle does not identify a subscription"));
    }

    return getFederate (handleInfo->local_fed_id)->getSubscription (handle_)->getData ();
}


const std::vector<Handle> &CommonCore::getValueUpdates (federate_id_t federateID)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    return fed->getEvents ();
}


Handle CommonCore::registerEndpoint (federate_id_t federateID, const std::string &name, const std::string &type)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    if (fed->getState () != HELICS_CREATED)
    {
        throw (invalidFunctionCall ());
    }

    std::unique_lock<std::mutex> lock (_mutex);
    auto fnd = endpoints.find (name);
    if (fnd != endpoints.end ())
    {
        throw (-1);  // TODO: make a set of exceptions;
    }
    auto id = getNewHandle ();
    endpoints.emplace (name, id);
    lock.unlock ();

    fed->createEndpoint (id, name, type);

    createBasicHandle (id, fed->global_id, fed->local_id, HANDLE_END, name, type, "", false);

    ActionMessage m (CMD_REG_END);
    m.source_id = fed->global_id;
    m.source_handle = id;
    m.name = name;
    m.info ().type = type;

    _queue.push (m);
    return id;
}


Handle CommonCore::registerSourceFilter (federate_id_t federateID,
                                         const std::string &filterName,
                                         const std::string &source,
                                         const std::string &type_in,
										const std::string &type_out)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    if (fed->getState () != HELICS_CREATED)
    {
        throw (invalidFunctionCall ());
    }

    auto id = getNewHandle ();
    fed->createSourceFilter (id, filterName, source, type_in);

    createBasicHandle (id, fed->global_id, fed->local_id, HANDLE_SOURCE_FILTER, filterName,source, type_in, type_out);

    ActionMessage m (CMD_REG_SRC_FILTER);
    m.source_id = fed->global_id;
    m.source_handle = id;
    m.name = filterName;
    m.info ().target = source;
    m.info ().type = type_in;
	m.info().type_out = type_out;

    std::unique_lock<std::mutex> lock (_handlemutex);
    auto fndend = endpoints.find (source);
    if (fndend != endpoints.end ())
    {
        auto endhandle = fndend->second;
        auto endid = handles[endhandle]->fed_id;
		handles[endhandle]->hasSourceFilter = true;
        lock.unlock ();
        m.processingComplete = true;
        // send to broker and core
        addCommand (m);
        // now send the same command to the endpoint
        m.dest_handle = endhandle;
        m.dest_id = endid;
        // send to
        addCommand (m);
    }
    else
    {
        lock.unlock ();
        //
        addCommand (m);
    }
    return id;
}


Handle CommonCore::registerDestinationFilter (federate_id_t federateID,
                                              const std::string &filterName,
                                              const std::string &dest,
                                              const std::string &type_in,
												const std::string &type_out)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID not valid"));
    }
    if (fed->getState () != HELICS_CREATED)
    {
        throw (invalidFunctionCall ());
    }

    auto id = getNewHandle ();
    fed->createDestFilter (id, filterName, dest, type_in);

    createBasicHandle (id, fed->global_id, fed->local_id, HANDLE_DEST_FILTER, filterName, dest,type_in, type_out);

    ActionMessage m (CMD_REG_DST_FILTER);
    m.source_id = fed->global_id;
    m.source_handle = id;
    m.name = filterName;
    m.info ().target = dest;
    m.info ().type = type_in;
	m.info().type_out = type_out;

    std::unique_lock<std::mutex> lock (_handlemutex);

    auto fndend = endpoints.find (dest);
    if (fndend != endpoints.end ())
    {
        auto endhandle = fndend->second;
        auto endid = handles[endhandle]->fed_id;
		if (handles[endhandle]->hasDestFilter)
		{
			throw(registrationFailure("endpoint " + dest + " already has a destination filter"));
		}
		handles[endhandle]->hasDestFilter = true;
        lock.unlock ();
        m.processingComplete = true;
        // send to broker and core
        addCommand (m);
        // now send the same command to the endpoint
        m.dest_handle = endhandle;
        m.dest_id = endid;
        // send to
        addCommand (m);
    }
    else
    {
        lock.unlock ();
        //
        addCommand (m);
    }
    return id;
}


void CommonCore::registerFrequentCommunicationsPair (const std::string &source, const std::string &dest)
{
    std::lock_guard<std::mutex> lock (_mutex);

    assert (isInitialized ());
    assert (false);
}

void CommonCore::addDependency (federate_id_t federateId, const std::string &federateName) {}

void CommonCore::send (Handle sourceHandle, const std::string &destination, const char *data, uint64_t length)
{
    auto hndl = getHandleInfo (sourceHandle);
    if (hndl == nullptr)
    {
        throw (invalidIdentifier ("handle is not valid"));
    }

    if (hndl->what != HANDLE_END)
    {
        throw (invalidIdentifier ("handle does not point to an endpoint"));
    }
    auto fed = getFederate (hndl->local_fed_id);
    ActionMessage m (CMD_SEND_MESSAGE);

    m.info ().orig_source = hndl->key;
    m.info ().source = hndl->key;
    m.source_handle = sourceHandle;
    m.source_id = hndl->fed_id;

    m.payload = std::string (data, length);
    m.info ().target = destination;
    m.actionTime = fed->grantedTime ();

    queueMessage (processMessage (hndl, m));
}


void CommonCore::sendEvent (Time time,
                            Handle sourceHandle,
                            const std::string &destination,
                            const char *data,
                            uint64_t length)
{
    auto hndl = getHandleInfo (sourceHandle);
    if (hndl == nullptr)
    {
        throw (invalidIdentifier ("handle is not valid"));
    }
    if (hndl->what != HANDLE_END)
    {
        throw (invalidIdentifier ("handle does not point to an endpoint"));
    }
    ActionMessage m (CMD_SEND_MESSAGE);
    m.source_handle = sourceHandle;
    m.source_id = hndl->fed_id;
    m.actionTime = time;
    m.payload = std::string (data, length);
    m.info ().orig_source = hndl->key;
    m.info ().source = hndl->key;
    m.info ().target = destination;


    queueMessage (processMessage (hndl, m));
}


void CommonCore::sendMessage (Handle sourceHandle, std::unique_ptr<Message> message)
{
    auto hndl = getHandleInfo (sourceHandle);
    if (hndl == nullptr)
    {
        throw (invalidIdentifier ("handle is not valid"));
    }
    if (hndl->what != HANDLE_END)
    {
        throw (invalidIdentifier ("handle does not point to an endpoint"));
    }
    ActionMessage m (CMD_SEND_MESSAGE);

    m.info ().orig_source = std::move (message->origsrc);

    if (hndl == nullptr)
    {
        m.info ().source = std::move (message->src);
    }
    else
    {
        m.info ().source = hndl->key;
        m.source_handle = hndl->id;
        m.source_id = hndl->fed_id;
    }
    m.payload = std::move (message->data.to_string ());
    m.info ().target = std::move (message->dest);
    m.actionTime = message->time;
    m.source_handle = sourceHandle;

    queueMessage (processMessage (hndl, m));
}

//Checks for filter operations
ActionMessage &CommonCore::processMessage (BasicHandleInfo *hndl, ActionMessage &m)
{
    if (hndl == nullptr)
    {
        return m;
    }
	if (hndl->hasSourceFilter)
	{
		auto filtFunc = getFilterCoordinator(hndl->id);
		if (filtFunc->hasSourceOperators)
		{
			
			for (int ii=0;ii <static_cast<int>(filtFunc->sourceOperators.size());++ii)
			{
				auto fed = getFederate(filtFunc->sourceOperators[ii].fed_id);
				if (fed != nullptr)
				{
					//deal with local source filters filters
					auto tempMessage = createMessage(std::move(m));
					auto FiltI = fed->getFilter(filtFunc->sourceOperators[ii].handle);
					assert(FiltI->filterOp != nullptr);
					tempMessage = FiltI->filterOp->process(std::move(tempMessage));
					m = ActionMessage(std::move(tempMessage));
				}
				else
				{
					m.dest_id = filtFunc->sourceOperators[ii].fed_id;
					m.dest_handle = filtFunc->sourceOperators[ii].handle;
					if ((ii < static_cast<int>(filtFunc->sourceOperators.size() - 1))|| (filtFunc->finalSourceFilter.fed_id != invalid_fed_id))
					{
						m.setAction(CMD_SEND_FOR_FILTER_OPERATION);
					}
					else
					{
						m.setAction(CMD_SEND_FOR_FILTER);
					}
					return m;
				}
				
			}
			
		}
		if (filtFunc->finalSourceFilter.fed_id!=invalid_fed_id)
		{
			m.setAction(CMD_SEND_FOR_FILTER);
			m.dest_id = filtFunc->finalSourceFilter.fed_id;
			m.dest_handle = filtFunc->finalSourceFilter.handle;
		}
	}
    
    return m;
}

void CommonCore::queueMessage (ActionMessage &message)
{
	switch (message.action())
	{
	case CMD_SEND_MESSAGE:
	{
		// Find the destination endpoint
		auto localP = getLocalEndpoint(message.info().target);
		if (localP == nullptr)
		{  // must be a remote endpoint push it to the main queue to deal with
			addCommand(message);
			return;
		}
		if (localP->hasDestFilter)  // the endpoint has a destination filter
		{
			auto ffunc = getFilterCoordinator(localP->id);
			assert(ffunc->hasDestOperator);
			auto FiltI = getFederate(ffunc->destOperator.fed_id)->getFilter(ffunc->destOperator.handle);
			assert(FiltI->filterOp != nullptr);

			auto tempMessage = createMessage(std::move(message));
			auto nmessage = FiltI->filterOp->process(std::move(tempMessage));

			message.moveInfo(std::move(nmessage));
		}
		message.dest_id = localP->fed_id;
		message.dest_handle = localP->id;
		addCommand(message);
	}
	break;
	case CMD_SEND_FOR_FILTER:
	case CMD_SEND_FOR_FILTER_OPERATION:
	{
		addCommand(message);
	}
	break;
	default:
		break;
	}
}


uint64_t CommonCore::receiveCount (Handle destination)
{
    auto fed = getHandleFederate (destination);
    if (fed == nullptr)
    {
        return 0;
    }
    return fed->getQueueSize (destination);
}


std::unique_ptr<Message> CommonCore::receive (Handle destination)
{
    auto fed = getHandleFederate (destination);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("invalid handle"));
    }
    if (fed->getState () != HELICS_EXECUTING)
    {
        return nullptr;
    }

    return fed->receive (destination);
}


std::unique_ptr<Message> CommonCore::receiveAny (federate_id_t federateID, Handle &endpoint_id)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("FederateID is not valid"));
    }
    if (fed->getState () != HELICS_EXECUTING)
    {
        endpoint_id = invalid_Handle;
        return nullptr;
    }
    return fed->receiveAny (endpoint_id);
}


uint64_t CommonCore::receiveCountAny (federate_id_t federateID)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("FederateID is not valid"));
    }
    if (fed->getState () != HELICS_EXECUTING)
    {
        return 0;
    }

    return fed->getQueueSize ();
}

void CommonCore::logMessage (federate_id_t federateID, int logLevel, const std::string &logMessage)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("FederateID is not valid"));
    }
    ActionMessage m (CMD_LOG);

    m.source_id = fed->global_id;
    m.index = logLevel;
    m.payload = logMessage;
    _queue.push (m);
    sendToLogger (federateID, logLevel, fed->getIdentifier(), logMessage);
}

void CommonCore::sendToLogger (federate_id_t federateID,
                               int logLevel,
                               const std::string &name,
                               const std::string &message) const
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ());
    }
    // TODO:: make federateState logging function
}

void CommonCore::setLoggingFunction (
  federate_id_t federateID,
  std::function<void(int, const std::string &, const std::string &)> logFunction)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("Invalid FederateID"));
    }
    fed->setLogger (std::move (logFunction));
}

void CommonCore::setFilterOperator (Handle filter, std::shared_ptr<FilterOperator> callback)
{
    auto hndl = getHandleInfo (filter);
    if (hndl == nullptr)
    {
        throw (invalidIdentifier ("filter is not a valid handle"));
    }
    if ((hndl->what != HANDLE_DEST_FILTER) && (hndl->what != HANDLE_SOURCE_FILTER))
    {
        throw (invalidIdentifier ("filter identifier does not point a filter"));
    }

    auto FiltI = getFederate (hndl->fed_id)->getFilter (filter);

    
	if (coreState < operating)
	{
		FiltI->filterOp = std::move(callback);
		if (hndl->what == HANDLE_SOURCE_FILTER)
		{
			ActionMessage cmd(CMD_SRC_FILTER_HAS_OPERATOR);
			cmd.source_id = hndl->fed_id;
			cmd.source_handle = hndl->id;
			if (FiltI->target.first != invalid_fed_id)
			{
				cmd.dest_id = FiltI->target.first;
				cmd.dest_handle = FiltI->target.second;
			}
			else
			{
				std::unique_lock<std::mutex> lock(_handlemutex);
				auto fndend = endpoints.find(hndl->target);
				if (fndend != endpoints.end())
				{
					auto endhandle = fndend->second;
					cmd.dest_id = handles[endhandle]->fed_id;
					cmd.dest_handle = endhandle;
				}
			}
			addCommand(cmd);
			
		}
		
	}
	else if (coreState == operating)
	{
		if (FiltI->filterOp)
		{
			FiltI->filterOp = std::move(callback);
		}
		else
		{
			throw(invalidFunctionCall(" filter operation can not be set in operating mode if it was not previously defined"));
		}
	}
	else
	{
		throw(invalidFunctionCall(" filter operation can not be set in current state"));
	}
}

FilterCoordinator *CommonCore::getFilterCoordinator (Handle id_)
{
	// only activate the lock if we not in an operating state
	auto lock = (coreState == operating) ? std::unique_lock<std::mutex>(_mutex, std::defer_lock) :
		std::unique_lock<std::mutex>(_mutex);
    auto fnd = filters.find (id_);
    if (fnd == filters.end ())
    {
		lock.unlock();
		if (coreState < operating)
		{

			// just make a dummy filterFunction so we have something to return
			auto ff = std::make_unique<FilterCoordinator>();
			auto ffp = ff.get();
			lock.lock();
			filters.emplace(id_, std::move(ff));
			return ffp;
		}
		else
		{
			return nullptr;
		}
      
    }
    else
    {
        return fnd->second.get ();
    }
}


uint64_t CommonCore::receiveFilterCount (federate_id_t federateID)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID is not valid"));
    }
    if (fed->getState () != HELICS_EXECUTING)
    {
        return 0;
    }

	return fed->getFilterQueueSize();
}

std::unique_ptr<Message> CommonCore::receiveAnyFilter (federate_id_t federateID, Handle &filter_id)
{
    auto fed = getFederate (federateID);
    if (fed == nullptr)
    {
        throw (invalidIdentifier ("federateID is not valid"));
    }
    if (fed->getState () != HELICS_EXECUTING)
    {
        filter_id = invalid_Handle;
        return nullptr;
    }
    return fed->receiveAnyFilter (filter_id);
}


void CommonCore::setIdentifier (const std::string &name)
{
    if (coreState == created)
    {
        std::lock_guard<std::mutex> lock (_mutex);
        identifier = name;
    }
    else
    {
        throw (invalidFunctionCall ("setIdentifier can only be called before the core is initialized"));
    }
}

std::string CommonCore::query(const std::string &target, const std::string &queryStr)
{
	return "";

}

void CommonCore::addCommand (const ActionMessage &m)
{
    if (isPriorityCommand (m))
    {
        processPriorityCommand (m);
    }
    else
    {
        // just route to the general queue;
        _queue.push (m);
    }
}

void CommonCore::addCommand(ActionMessage &&m)
{
	if (isPriorityCommand(m))
	{
		processPriorityCommand(m);
	}
	else
	{
		// just route to the general queue;
		_queue.emplace(std::move(m));
	}
}

void CommonCore::queueProcessingLoop ()
{
    while (true)
    {
        auto command = _queue.pop ();
        // LOG (INFO) << "\"\"\"" << command << std::endl << "\"\"\"" << ENDL;

        switch (command.action ())
        {
        case CMD_IGNORE:
            break;
		case CMD_TERMINATE_IMMEDIATELY:
			return; //immediate return
        case CMD_STOP:
            processCommand (command);
            return disconnect ();  // this can potential cause object destruction so do nothing after this call
        default:
            processCommand (command);
        }
    }
}


void CommonCore::processPriorityCommand(const ActionMessage &command)
{
    // deal with a few types of message immediately
    switch (command.action ())
    {
    case CMD_REG_FED:
    case CMD_REG_BROKER:
        // These really shouldn't happen here probably means something went wrong in setup but we can handle it
        // forward the connection request to the higher level
        transmit (0, command);
        break;
    case CMD_BROKER_ACK:
        if (command.payload == identifier)
        {
            if (!command.error)
            {
                global_broker_id = command.dest_id;
                transmitDelayedMessages ();
                return;
            }
            else
            {
                // generate error messages in response to all the delayed messages
            }
        }
        break;
    case CMD_FED_ACK:
    {
        auto id = getFederateId (command.name);
        if (id != invalid_fed_id)
        {
            auto fed = getFederate (id);
            if (fed == nullptr)
            {
                break;
            }
            // now add the new global id to the translation table
            {  // scope for the lock
                std::lock_guard<std::mutex> lock (_mutex);
                global_id_translation.emplace (command.dest_id, fed->local_id);
            }
            // push the command to the local queue
            fed->addAction (command);
        }
    }
    break;
    case CMD_REG_ROUTE:
        // TODO:: double check this
        addRoute (command.dest_handle, command.payload);
        break;
	case CMD_DISCONNECT:
		if (allDisconnected())
		{
			coreState = core_state_t::terminated;
			ActionMessage dis(CMD_DISCONNECT);
			dis.source_id = global_broker_id;
			transmit(0, dis);
			addCommand(CMD_STOP);
		}
		break;
		
	case CMD_PRIORITY_ACK:
	case CMD_ROUTE_ACK:
		break;
	default:
	{
		if (!isPriorityCommand(command))
		{
			//make a copy and go through the regular processing
			ActionMessage cmd(command);
			processCommand(cmd);
		}
		
	}
		
	//case CMD_DISCONNECT_ACK:
	//	break;
    }
}


void CommonCore::transmitDelayedMessages ()
{
    auto msg = delayTransmitQueue.pop ();
    while (msg)
    {
        msg->source_id = global_broker_id;
        transmit (0, *msg);
        msg = delayTransmitQueue.pop ();
    }
}

void CommonCore::processCommand (ActionMessage &command)
{
    // LOG (INFO) << "\"\"\"" << command << std::endl << "\"\"\"" << ENDL
    switch (command.action ())
    {
    case CMD_IGNORE:
        break;
    case CMD_STOP:
		if (isConnected())
		{
			if (!allDisconnected())
			{  // only send a disconnect message if we haven't done so already
				ActionMessage m(CMD_DISCONNECT);
				m.source_id = global_broker_id;
				transmit(0, m);
			}
		}
        break;
    case CMD_TIME_REQUEST:
    case CMD_TIME_GRANT:
    case CMD_EXEC_GRANT:
    case CMD_EXEC_REQUEST:
    {
        if (command.dest_id == 0)
        {
            // route the message to all dependent feds
            auto fed = getFederate (command.source_id);
            if (fed == nullptr)
            {
                return;
            }
            auto &dep = fed->getDependents ();
            for (auto &fed_id : dep)
            {
                routeMessage (command, fed_id);
            }
        }
        else
        {
            routeMessage (command);
        }
    }
    break;
	case CMD_ADD_DEPENDENCY:
	case CMD_REMOVE_DEPENDENCY:
	case CMD_ADD_DEPENDENT:
	case CMD_REMOVE_DEPENDENT:
		routeMessage(command);
		break;
    case CMD_SEND_FOR_FILTER:
        routeMessage (command);
        break;
	case CMD_SEND_FOR_FILTER_OPERATION:
		break;
    case CMD_PUB:
    {
        // route the message to all the subscribers
        auto pubInfo = getFederate (command.source_id)->getPublication (command.source_handle);
        for (auto &subscriber : pubInfo->subscribers)
        {
            command.dest_id = subscriber.first;
            command.dest_handle = subscriber.second;
            routeMessage (command);
        }
    }
    break;
   
    case CMD_LOG:
    case CMD_ERROR:
        routeMessage (command);
        break;
    case CMD_REG_SUB:
        // for these registration filters any processing is already done in the
        // registration functions so this is just a router
        if (command.dest_id != 0)
        {
            auto fed = getFederate (command.dest_id);
            if (fed != nullptr)
            {
                fed->addAction (command);
                auto pubhandle = getHandleInfo (command.dest_handle);
                if (pubhandle != nullptr)
                {
                    pubhandle->used = true;
                }
            }
        }
        else
        {
            transmit (0, command);
        }

        break;
    case CMD_REG_END:
        // for these registration filters any processing is already done in the
        // registration functions so this is just a router
        if (command.dest_id != 0)
        {
            auto fed = getFederate (command.dest_id);
            if (fed != nullptr)
            {
                fed->addAction (command);
                auto filthandle = getHandleInfo (command.dest_handle);
                if (filthandle != nullptr)
                {
                    if ((filthandle->what == HANDLE_DEST_FILTER) || (filthandle->what == HANDLE_SOURCE_FILTER))
                    {
                        filthandle->used = true;
                    }
                }
            }
        }
        else
        {
            transmit (0, command);
        }

        break;
    case CMD_REG_PUB:
		// for these registration filters any processing is already done in the
		// registration functions so this is just a router
		routeMessage(command);
		break;
    case CMD_REG_DST_FILTER:
    case CMD_REG_SRC_FILTER:
		// for these registration filters any processing is already done in the
		// registration functions so this is just a router
		routeMessage(command);
		if (command.dest_id != 0)
		{
			processFilterInfo(command);
		}
		break;

    case CMD_NOTIFY_SUB:
    {
        // just forward these to the appropriate federate
        auto fed = getFederate (command.dest_id);
        if (fed != nullptr)
        {
            fed->addAction (command);
            auto pubhandle = getHandleInfo (command.dest_handle);
            if (pubhandle != nullptr)
            {
                pubhandle->used = true;
            }
        }
    }
    break;
    case CMD_NOTIFY_END:
    {
        // just forward these to the appropriate federate
        auto fed = getFederate (command.dest_id);
        if (fed != nullptr)
        {
            fed->addAction (command);
            auto filthandle = getHandleInfo (command.dest_handle);
            if (filthandle != nullptr)
            {
                if ((filthandle->what == HANDLE_DEST_FILTER) || (filthandle->what == HANDLE_SOURCE_FILTER))
                {
                    filthandle->used = true;
                }
            }
        }
    }
    break;
    case CMD_NOTIFY_PUB:
		routeMessage(command);
		break;
	case CMD_NOTIFY_SRC_FILTER:
	{
		auto endhandle = getHandleInfo(command.dest_handle);
		if (endhandle != nullptr)
		{
			endhandle->hasSourceFilter = true;
		}

		auto fed = getFederate(command.dest_id);
		if (fed != nullptr)
		{
			fed->addAction(command);
		}
		processFilterInfo(command);
	}
	break;
	case CMD_SRC_FILTER_HAS_OPERATOR:
		if (command.dest_id == 0)
		{
			transmit(0, command);
		}
		else
		{
			processFilterInfo(command);
		}
		break;
    case CMD_NOTIFY_DST_FILTER:
    {
		auto endhandle = getHandleInfo(command.dest_handle);
		if (endhandle != nullptr)
		{
			if (!endhandle->hasDestFilter)
			{
				endhandle->hasDestFilter = true;
			}
			else
			{
				ActionMessage err(CMD_ERROR);
				err.source_id = command.dest_id;
				err.source_handle = command.dest_handle;
				err.payload = "Endpoint " + endhandle->key + " already has a destination filter";
				transmit(0, err);
				break;
			}
		}

        auto fed = getFederate (command.dest_id);
        if (fed != nullptr)
        {
            fed->addAction (command);
			
        }
		processFilterInfo(command);
    }
    break;
    case CMD_INIT:
	{
		auto fed = getFederate(command.source_id);
		if (fed != nullptr)
		{
			fed->init_transmitted = true;
			if (allInitReady())
			{
				core_state_t exp = connected;
				if (coreState.compare_exchange_strong(exp, core_state_t::initializing))
				{// make sure we only do this once
					command.source_id = global_broker_id;
					organizeFilterOperations();
					transmit(0, command);
				}
			}

		}

		
	}
        break;
    case CMD_INIT_GRANT:
        {
            core_state_t exp = initializing;
            if (coreState.compare_exchange_strong (exp, core_state_t::operating))
            {// forward the grant to all federates
                for (auto &fed : _federates)
                {
                    fed->addAction (command);
                }
            }
        }
        break;

    case CMD_SEND_MESSAGE:
    {
        auto fnd = endpoints.find (command.info ().target);
        if (fnd != endpoints.end ())
        {  // destination is local
            auto fed = getHandleFederate (fnd->second);
            fed->addAction (command);
            command.dest_id = fed->global_id;
            command.dest_handle = fnd->second;
        }
        else
        {
            auto kfnd = knownExternalEndpoints.find (command.info ().target);
            if (kfnd != knownExternalEndpoints.end ())
            {  // destination is known
                auto route = getRoute (kfnd->second);
                transmit (route, command);
            }
            else
            {
                transmit (0, command);
            }
        }
    }
    break;
    default:
        if (isPriorityCommand (command))
        {  // this is a backup if somehow one of these message got here
            processPriorityCommand (command);
        }
        break;
    }
}


void CommonCore::processFilterInfo(ActionMessage &command)
{
	auto filterInfo = getFilterCoordinator(command.dest_handle);
	if (filterInfo == nullptr)
	{
		return;
	}
	switch (command.action())
	{
	case CMD_REG_DST_FILTER:
	case CMD_NOTIFY_DST_FILTER:
		filterInfo->destOperator.fed_id = command.source_id;
		filterInfo->destOperator.handle = command.source_handle;
		filterInfo->hasDestOperator = true;
		filterInfo->destOperator.input_type = command.info().type;
		filterInfo->destOperator.output_type = command.info().type_out;
		break;
	case CMD_REG_SRC_FILTER:
	case CMD_NOTIFY_SRC_FILTER:
		filterInfo->allSourceFilters.emplace_back(command.source_id, command.source_handle, command.info().type, command.info().type_out);
		filterInfo->hasSourceFilter = true;
		if (command.flag)
		{
			filterInfo->allSourceFilters.back().hasOperator_flag = true;
		}
		break;
	case CMD_SRC_FILTER_HAS_OPERATOR:
		for (auto &filt : filterInfo->allSourceFilters)
		{
			if ((filt.fed_id == command.source_id) && (filt.handle == command.source_handle))
			{
				filt.hasOperator_flag = true;
				filterInfo->hasSourceOperators = true;
				break;
			}
		}
		break;
	default:
		//all other commands do not impact filters
		break;
	}
}

void CommonCore::organizeFilterOperations()
{
	for (auto &filter : filters)
	{
		auto *fi = filter.second.get();
		auto *handle = getHandleInfo(filter.first);
		if (handle == nullptr)
		{
			continue;
		}
		std::string endpointType = handle->type;

		if (!fi->allSourceFilters.empty())
		{
			int opCount = 0;
			int nonOpCount = 0;
			for (auto &filt : fi->allSourceFilters)
			{
				if (filt.hasOperator_flag)
				{
					++opCount;
				}
				else
				{
					++nonOpCount;
				}
			}
			if (nonOpCount == 1)
			{
				for (auto &filt : fi->allSourceFilters)
				{
					if (!filt.hasOperator_flag)
					{
						fi->finalSourceFilter = filt;
						break;
					}
					
				}
			}
			fi->sourceOperators.clear();
			if (opCount <= 1)
			{
				for (auto &filt : fi->allSourceFilters)
				{
					if (filt.hasOperator_flag)
					{
						fi->sourceOperators.push_back(filt);
						break;
					}

				}
			}
			else
			{
				//Now we have to do some intelligent ordering with types
				std::vector<bool> used(fi->allSourceFilters.size(),false);
				bool someUnused = true;
				std::string currentType = endpointType;
				while (someUnused)
				{
					someUnused = false;
					for (size_t ii = 0; ii < fi->allSourceFilters.size(); ++ii)
					{
						if (!fi->allSourceFilters[ii].hasOperator_flag)
						{
							used[ii] = true;
							continue;
						}
						if (used[ii])
						{
							continue;
						}
						//TODO:: this will need some work to finish sorting out but should work for initial tests
						if (fi->allSourceFilters[ii].input_type == currentType)
						{
							used[ii] = true;
							someUnused = true;
							fi->sourceOperators.push_back(fi->allSourceFilters[ii]);
							currentType = fi->allSourceFilters[ii].output_type;
						}
						else
						{
							someUnused = true;
						}
						
					}
				}
			}
		}
	}
}

void CommonCore::routeMessage (ActionMessage &cmd, federate_id_t dest)
{
	if (dest == 0)
	{
		cmd.dest_id = 0;
		transmit(0, cmd);
	}
    else if (isLocal (dest))
    {
        auto fed = getFederate (dest);
        if (fed != nullptr)
        {
            fed->addAction (cmd);
        }
    }
    else
    {
        auto route = getRoute (dest);
        cmd.dest_id = dest;
        transmit (route, cmd);
    }
}

void CommonCore::routeMessage (const ActionMessage &cmd)
{
	if (cmd.dest_id == 0)
	{
		transmit(0, cmd);
	}
    else if (isLocal (cmd.dest_id))
    {
        auto fed = getFederate (cmd.dest_id);
        if (fed != nullptr)
        {
            fed->addAction (cmd);
        }
    }
    else
    {
        auto route = getRoute (cmd.dest_id);
        transmit (route, cmd);
    }
}

}  // namespace helics