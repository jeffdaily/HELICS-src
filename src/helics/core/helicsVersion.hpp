/*

Copyright (C) 2017-2018, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle Memorial
Institute; the National Renewable Energy Laboratory, operated by the Alliance for Sustainable Energy, LLC; and the
Lawrence Livermore National Laboratory, operated by Lawrence Livermore National Security, LLC.

*/
#ifndef _HELICS_VERSION_
#define _HELICS_VERSION_
#pragma once

#include "helics/helics-config.h"
#include <string>

/** @file
file linking with version info and containing some convenience functions
*/
namespace helics
{
/** @returns a string containing version information*/
std::string helicsVersionString ();

/** get the Major version number*/
inline int helicsVersionMajor () { return HELICS_VERSION_MAJOR; }
/** get the Minor version number*/
inline int helicsVersionMinor () { return HELICS_VERSION_MINOR; }
/** get the patch number*/
inline int helicsVersionPatch () { return HELICS_VERSION_PATCH; }
}

#endif /*_HELICS_VERSION_*/
