/**
 * @file ACCReceiverSatellite.hpp
 * @brief Implementation of random data transmitting satellite
 *
 * @copyright Copyright (c) 2024 DESY and the Constellation authors.
 * This software is distributed under the terms of the EUPL-1.2 License, copied verbatim in the file "LICENSE.md".
 * SPDX-License-Identifier: EUPL-1.2
 */

#include "ACCReceiverSatellite.hpp"

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
#include "constellation/satellite/ReceiverSatellite.hpp"
#include "constellation/core/message/CDTP1Message.hpp"
#include "constellation/core/protocol/CSCP_definitions.hpp"
#include "ACC.h"

using namespace constellation::config;
using namespace constellation::satellite;
using namespace constellation::utils;



ACCReceiverSatellite::ACCReceiverSatellite(std::string_view type, std::string_view name)
    : ReceiverSatellite(type, name)
{ 
    // support_reconfigure();
    // acc_.setName(name);
    // acc_.setType(type);
}

void ACCReceiverSatellite::initializing(constellation::config::Configuration& config)
{
base_path_ = config.getPath("output_directory");
LOG(DEBUG)  << "Base path: " << base_path_;
validate_output_directory(base_path_);
LOG(DEBUG) << "Validating output directory: " << base_path_;


std::string timestamp = "";

}

void ACCReceiverSatellite::launching(){
    LOG(INFO) << "Launching";
    LOG(INFO) << "Get Status";
    submit_status(std::string(getStatus()));

}


void ACCReceiverSatellite::starting(std::string_view run_identifier)
{
    LOG(DEBUG) << "Starting ACC Receiver Satellite with run identifier: ";
    // TODO: set as parameter (config)
    // std::filesystem::path base_path_ = "~/constellation/results";
    std::string fileName = acc_.nameFile();
    LOG(INFO) << "Create File" << run_identifier;
    // filename const
    file_ = create_output_file(base_path_, fileName + "_" + std::string(run_identifier), "raw", true);
    if (!file_.is_open()) {
        throw SatelliteError("Could not open output file for writing");
    }
    LOG(INFO) << "Output path: " << base_path_;
    LOG(INFO) << "Final file name: " << fileName + std::string(run_identifier) << ".raw";
    LOG(INFO) << "Starting " << run_identifier;
    
    

}

void ACCReceiverSatellite::receive_bor(const constellation::message::CDTP1Message::Header& header, constellation::config::Configuration config){

}


void ACCReceiverSatellite::receive_eor(const constellation::message::CDTP1Message::Header& header, constellation::config::Dictionary run_metadata){

}



void ACCReceiverSatellite::receive_data(constellation::message::CDTP1Message data_message){



    // LOG(INFO) << "Received Data message";
    const auto& header = data_message.getHeader();
    // LOG(INFO) << "Writing data event";
    LOG(INFO) << "Received data message from " << header.getSender();
    for (const auto& buffer : data_message.getPayload()) {
        std::span<const std::byte> span = buffer.span();
        file_.write(reinterpret_cast<const char*>(span.data()), static_cast<std::streamsize>(span.size()));
    }
    LOG(DEBUG) << "Wrote " << data_message.getPayload().size() << " bytes to file";
    if(!file_.good()) {
        throw SatelliteError("Error writing to file");
    }

    // reassign?


}



void ACCReceiverSatellite::stopping()
{
    //acc_.joinDAQThread();

    LOG(INFO)<<"Stopping";
    file_.close();
    LOG(INFO)<<"Stopped";
    
}

void ACCReceiverSatellite::landing(std::string_view run_identifier)
{
    // nothing?
    LOG(INFO)<<"Landing"<< run_identifier;

    
}



