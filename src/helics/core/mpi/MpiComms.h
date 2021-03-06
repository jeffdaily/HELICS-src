/*

Copyright (C) 2017, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle Memorial Institute; the National Renewable Energy Laboratory, operated by the Alliance for Sustainable Energy, LLC; and the Lawrence Livermore National Laboratory, operated by Lawrence Livermore National Security, LLC.

*/
#ifndef _HELICS_MPI_COMMS_
#define _HELICS_MPI_COMMS_
#pragma once

#include "helics/core/CommsInterface.h"
#include <atomic>

namespace helics {

/** implementation for the core that uses zmq messages to communicate*/
class MpiComms:public CommsInterface {

public:
	/** default constructor*/
	MpiComms() = default;
	MpiComms(const std::string &brokerTarget, const std::string &localTarget);
	/** destructor*/
	~MpiComms();
private:
	virtual void queue_rx_function() override;	//!< the functional loop for the receive queue
	virtual void queue_tx_function() override;  //!< the loop for transmitting data
	virtual void closeTransmitter() override; //!< function to instruct the transmitter loop to close
	virtual void closeReceiver() override;  //!< function to instruct the receiver loop to close
};


} // namespace helics

#endif /* _HELICS_IPC_COMMS_ */

