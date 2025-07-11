/**
 * @file ACCTransmitterSatellite.hpp
 * @brief Implementation of random data transmitting satellite
 *
 * @copyright Copyright (c) 2024 DESY and the Constellation authors.
 * This software is distributed under the terms of the EUPL-1.2 License, copied verbatim in the file "LICENSE.md".
 * SPDX-License-Identifier: EUPL-1.2
 */

#include "ACCTransmitterSatellite.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <random>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

#include "constellation/core/config/Configuration.hpp"
#include "constellation/core/log/log.hpp"
#include "constellation/core/utils/string.hpp"
#include "constellation/satellite/TransmitterSatellite.hpp"
#include "ACC.h"

using namespace constellation::config;
using namespace constellation::satellite;
using namespace constellation::utils;



ACCTransmitterSatellite::ACCTransmitterSatellite(std::string_view type, std::string_view name)
    : TransmitterSatellite(type, name)
{ support_reconfigure();
    // acc_.setName(name);
    // acc_.setType(type);
}

void ACCTransmitterSatellite::initializing(constellation::config::Configuration& config)
{
}

void ACCTransmitterSatellite::launching(){
    LOG(INFO)<<"Launching";
    LOG(INFO)<<"Get Status";
    submit_status(std::string(getStatus()));
// acc_.createAcdcs();
// acc_.whichAcdcsConnected();
// acc_.setHardwareTrigSrc(1, 0xff); 
// acc_.toggleCal(1, 0x7FFF, 0xff);
// setPedestals(unsigned int boardmask, unsigned int chipmask, unsigned int adc); 
// acc_.setPedestals(0x7FFF, 0xff, 0xff);
    LOG(INFO)<<"VDD_DLL setting";
    vector<uint32_t> vdd_dll_vec(5, 0x1f);
    acc_.setVddDLL(vdd_dll_vec, true);

}

void ACCTransmitterSatellite::reconfiguring(const constellation::config::Configuration& partial_config)
{
    LOG(INFO)<<"Reconfiguring";
    acc_.parseConfig(partial_config);
    LOG(INFO)<<"Reinitializing for run";
    acc_.initializeForDataReadout(partial_config, "");

}

void ACCTransmitterSatellite::starting(std::string_view run_identifier)
{
    // write new method for data transmission
    LOG(INFO)<<"Starting DAQ thread"<< run_identifier;
    acc_.startRun();
    
    //acc_.startDAQThread();
}

void ACCTransmitterSatellite::running(const std::stop_token& stop_token)
{
    LOG(INFO)<<"Running, Listening Data";
    acc_.listenForAcdcData();
    while(!stop_token.stop_requested()) {
        // Do work
        if(stop_token.stop_requested()) {
            // Handle stop request
            LOG(INFO)<<"Ending Run";
            acc_.stopRun();
            break;
        }
    }
}


void ACCTransmitterSatellite::stopping()
{
    //acc_.joinDAQThread();

    LOG(INFO)<<"Stopping";
    acc_.endRun();
    LOG(INFO)<<"Stopped";
    
}

void ACCTransmitterSatellite::landing(std::string_view run_identifier)
{
    // nothing?
    LOG(INFO)<<"Landing"<< run_identifier;

    
}


// // Logging a message with the given level:
// LOG(INFO) << Received configuration";

// // Logging a message only once (e.g. in a loop):
// LOG_ONCE(WARNING) << "This message appears only once even if the statement "
//                   << "is executed many times";

// // Logging a message N times:
// LOG_N(STATUS, 3) << "This message is logged at most 3 times.";

// // Logging only when a condition evaluates to true:
// LOG_IF(WARNING, parameter == 5) << "Parameter 5 is set "
//                                << "- be careful when opening the box!";

// // Logging a message only every Nth time:
// LOG_NTH(STATUS, 100) << "This message is logged every 100th call to the logging macro.";

// // Logging a message only every T seconds:
// LOG_T(DEBUG, 5s) << "This message is logged at most every 5s";
