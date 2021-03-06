##############################################################################
#Copyright (C) 2017-2018, Battelle Memorial Institute
#All rights reserved.

#This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle Memorial Institute; the National Renewable Energy Laboratory, operated by the Alliance for Sustainable Energy, LLC; and the Lawrence Livermore National Laboratory, operated by Lawrence Livermore National Security, LLC.
##############################################################################
set(SRC_FILES
    CoreFactory.cpp
	BrokerFactory.cpp
	BrokerBase.cpp
	CommonCore.cpp
	FederateState.cpp
	SubscriptionInfo.cpp
	PublicationInfo.cpp
	#FilterInfo.cpp
	EndpointInfo.cpp
	ActionMessage.cpp
	CoreBroker.cpp
	core-types.cpp
	core-data.cpp
	TimeCoordinator.cpp
	TimeDependencies.cpp
	CommsInterface.cpp
	CommsBroker.cpp
	NetworkBrokerData.cpp
	helicsVersion.cpp
	helics-time.cpp
	HandleManager.cpp
)

set(TESTCORE_SOURCE_FILES
	TestBroker.cpp
	TestCore.cpp
)

set (IPC_SOURCE_FILES
	ipc/IpcCore.cpp
	ipc/IpcBroker.cpp
	ipc/IpcComms.cpp
	ipc/IpcQueueHelper.cpp
)

set (MPI_SOURCE_FILES
	mpi/MpiCore.cpp
	mpi/MpiBroker.cpp
	mpi/MpiComms.cpp
)

set (ZMQ_SOURCE_FILES
	zmq/ZmqCore.cpp
	zmq/ZmqBroker.cpp
	zmq/ZmqComms.cpp
	zmq/ZmqRequestSets.cpp
)

set (UDP_SOURCE_FILES
	udp/UdpCore.cpp
	udp/UdpBroker.cpp
	udp/UdpComms.cpp
)

set (TCP_SOURCE_FILES
	tcp/TcpCore.cpp
	tcp/TcpBroker.cpp
	tcp/TcpComms.cpp
	tcp/TcpHelperClasses.cpp
)

set (PUBLIC_INCLUDE_FILES
Core.hpp
Broker.hpp
CoreFactory.hpp
BrokerFactory.hpp
core-exceptions.hpp
core-types.hpp
core-data.hpp
helics-time.hpp
CoreFederateInfo.hpp
helicsVersion.hpp
../flag-definitions.h
)

set(INCLUDE_FILES
	BrokerBase.hpp
	TimeDependencies.hpp
	TimeCoordinator.hpp
	loggingHelper.hpp
	CommsBroker.hpp
	CommsBroker_impl.hpp
	CoreBroker.hpp
	ActionMessageDefintions.hpp
	ActionMessage.hpp
	CommonCore.hpp
	CommsInterface.hpp
	FederateState.hpp
	SubscriptionInfo.hpp
	PublicationInfo.hpp
	EndpointInfo.hpp
	FilterInfo.hpp
	BasicHandleInfo.hpp
	FilterFunctions.hpp
	NetworkBrokerData.hpp
	HandleManager.hpp
)

set(TESTCORE_HEADER_FILES
 TestCore.h
 TestBroker.h
)

set(IPC_HEADER_FILES
	ipc/IpcCore.h
	ipc/IpcBroker.h
	ipc/IpcComms.h
	ipc/IpcQueueHelper.h
)

set(ZMQ_HEADER_FILES
	zmq/ZmqCore.h
	zmq/ZmqBroker.h
	zmq/ZmqComms.h
	zmq/ZmqRequestSets.h
)

set(MPI_HEADER_FILES
	mpi/MpiCore.h
	mpi/MpiBroker.h
	mpi/MpiComms.h
	)

set(UDP_HEADER_FILES
	udp/UdpCore.h
	udp/UdpBroker.h
	udp/UdpComms.h
	)

set(TCP_HEADER_FILES
	tcp/TcpCore.h
	tcp/TcpBroker.h
	tcp/TcpComms.h
	tcp/TcpHelperClasses.h
	)

	list(APPEND SRC_FILES ${TESTCORE_SOURCE_FILES} ${UDP_SOURCE_FILES} ${IPC_SOURCE_FILES})
	list(APPEND INCLUDE_FILES ${TESTCORE_HEADER_FILES} ${UDP_HEADER_FILES} ${IPC_HEADER_FILES})

	if (NOT DISABLE_TCP_CORE)
		list(APPEND SRC_FILES ${TCP_SOURCE_FILES})
		list(APPEND INCLUDE_FILES ${TCP_HEADER_FILES} )
	endif()
	

if (${ZeroMQ_FOUND})
   list(APPEND SRC_FILES ${ZMQ_SOURCE_FILES})
   list(APPEND INCLUDE_FILES ${ZMQ_HEADER_FILES})
endif (${ZeroMQ_FOUND})

if (${MPI_C_FOUND})
   list(APPEND SRC_FILES ${MPI_SOURCE_FILES})
   list(APPEND INCLUDE_FILES ${MPI_HEADER_FILES})
endif (${MPI_C_FOUND})

include_directories(SYSTEM ${Boost_INCLUDE_DIR})
add_library(helics_core OBJECT ${SRC_FILES} ${INCLUDE_FILES} ${PUBLIC_INCLUDE_FILES})

if (${MPI_C_FOUND})
source_group("mpi" FILES ${MPI_SOURCE_FILES} ${MPI_HEADER_FILES})
endif (${MPI_C_FOUND})
source_group("zmq" FILES ${ZMQ_SOURCE_FILES} ${ZMQ_HEADER_FILES})
source_group("udp" FILES ${UDP_SOURCE_FILES} ${UDP_HEADER_FILES})
source_group("ipc" FILES ${IPC_SOURCE_FILES} ${IPC_HEADER_FILES})
source_group("test" FILES ${TESTCORE_SOURCE_FILES} ${TESTCORE_HEADER_FILES})

if (NOT DISABLE_TCP_CORE)
   source_group("tcp" FILES ${TCP_SOURCE_FILES} ${TCP_HEADER_FILES})
endif()

include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/ThirdParty)

install (FILES ${PUBLIC_INCLUDE_FILES} DESTINATION include/helics/core)

OPTION(BUILD_BROKER "Build a helics Broker" ON)
if (BUILD_BROKER)
add_executable(helics_broker helics-broker.cpp)
   target_link_libraries(helics_broker helics-static)
   set_target_properties (helics_broker PROPERTIES FOLDER apps)
   set(HELICS_BROKER_LOC ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "build folder location of the broker")
   #message(STATUS "bloc ${CMAKE_CURRENT_BINARY_DIR}")
   INSTALL(TARGETS helics_broker DESTINATION bin)
foreach(keyfile IN LISTS KEY_LIBRARY_FILES)
add_custom_command(TARGET helics_broker POST_BUILD        # Adds a post-build event to core tests
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  # which executes "cmake - E copy_if_different..."
        "${keyfile}"      # <--this is in-file
        "$<TARGET_FILE_DIR:helics_broker>/")                 # <--this is out-file path
endforeach(keyfile)
endif()


