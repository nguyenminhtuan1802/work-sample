#include "ScenarioConfiguration.h"

namespace sse::ScenarioConfiguration {
const std::unordered_map<std::string, unsigned int> GetTimeoutValues() {
    std::unordered_map<std::string, unsigned int> mTimeOutValues = {
        {   "SimulationStartEvent",     SIMULATION_START_EVENT},
        {    "SimulationStepEvent",      SIMULATION_STEP_EVENT},
        {"SimulationPostStepEvent", SIMULATION_POST_STEP_EVENT},
        {    "SimulationStopEvent",      SIMULATION_STOP_EVENT},
        {       "CreateActorEvent",         CREATE_ACTOR_EVENT},
        {      "DestroyActorEvent",        DESTROY_ACTOR_EVENT}
    };

    return mTimeOutValues;
}

sseProto::SimulationSettings GetDefaultSimulationSettings() {
    sseProto::SimulationSettings simSettings;

    // Default values for simulation settings
    simSettings.set_step_size(DEFAULT_STEP_SIZE);
    simSettings.set_max_simulation_time(DEFAULT_MAX_SIM_TIME);
    simSettings.set_is_pacer_on(DEFAULT_PACER_STATUS);
    simSettings.set_simulation_pace(DEFAULT_SIMULATION_PACING);

    return simSettings;
}
} // namespace sse::ScenarioConfiguration
