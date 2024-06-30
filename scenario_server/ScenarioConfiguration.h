#pragma once
#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"

////////////////////////////////////////////////////////////////////////////////
// Holds the scenario simulation configuration parameters that are read from an
// xml file ("Project/SimulationConfiguration.xml"). The values from the xml are
// cached, the first time any member function of this class is called. In case
// the we fail to read the xml, the default values for the parameters are returned.
//  - Non-thread safe singleton class.
//
namespace sse::ScenarioConfiguration {
// Default constants
static const double DEFAULT_STEP_SIZE                      = 0.02;
static const double DEFAULT_MAX_SIM_TIME                   = 100;
static const double DEFAULT_SIMULATION_PACING              = 1.0;
static const bool   DEFAULT_PACER_STATUS                   = true;
static const double DEFAULT_SPEED_COMPARE_TOLERANCE        = 0.1;
static const double DEFAULT_LONGI_OFFSET_COMPARE_TOLERANCE = 0.5;
static const double DEFAULT_MAX_SEARCH_DISTANCE            = 0.5;

// Default ports
static const unsigned int DEFAULT_COSIM_PORT = 35706;

// Default time-out values
static const unsigned int SIMULATION_START_EVENT     = 30000;
static const unsigned int SIMULATION_STEP_EVENT      = 6000;
static const unsigned int SIMULATION_POST_STEP_EVENT = 6000;
static const unsigned int SIMULATION_STOP_EVENT      = 6000;
static const unsigned int CREATE_ACTOR_EVENT         = 60000;
static const unsigned int DESTROY_ACTOR_EVENT        = 10000;

const std::unordered_map<std::string, unsigned int> GetTimeoutValues();

sseProto::SimulationSettings GetDefaultSimulationSettings();

// Returns the co-simulation server port from SimulationConfiguration.xml
static inline unsigned int GetCoSimServerPort() {
    return DEFAULT_COSIM_PORT;
}

// Returns the simulation constants
static inline double GetMaxSimulationTime() {
    return DEFAULT_MAX_SIM_TIME;
}

static inline double GetSimulationStepSize() {
    return DEFAULT_STEP_SIZE;
}

static inline double GetSimulationPacing() {
    return DEFAULT_SIMULATION_PACING;
}

static inline double GetSpeedComparisonTolerance() {
    return DEFAULT_SPEED_COMPARE_TOLERANCE;
}

static inline double GetLongitudinalOffsetComparisonTolerance() {
    return DEFAULT_LONGI_OFFSET_COMPARE_TOLERANCE;
}

static inline double GetMaxSearchDistance() {
    return DEFAULT_MAX_SEARCH_DISTANCE;
}

// static sseProto::SimulationSettings GetDefaultSimulationSettings();
} // namespace sse::ScenarioConfiguration
