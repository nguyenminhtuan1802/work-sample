#include "mw_grpc_clients/scenario_server/ScenarioExecutor.hpp"
#include "mw_grpc_clients/scenario_server/SSEServerInstantiator.hpp"

#include "Action.h"
#include "Phase.h"
#include "PhaseDiagramExecutor.h"
#include "RoadRunnerConversions.h"
#include "Simulation.h"
#include "Utility.h"

using namespace sseProto;

namespace sse {
////////////////////////////////////////////////////////////////////////////////

ScenarioExecutor::ScenarioExecutor(const Actor& worldActor, Simulation* sim)
    : mSimulation(sim) {
    // World actor shall already been validated before creating the scenario
    // executor. Assert if it is still invalid.
    rrThrowVerify(worldActor.actor_spec().has_world_spec(), "Input argument must be a world actor.");
    auto& worldSpec = worldActor.actor_spec().world_spec();

    // We allow a world to not have a scenario for testing of partially built
    // model. Actors will keep stationary during simulation, due to the lack of
    // scenario events/commands (e.g. actions, modifiers)
    if (worldSpec.has_scenario()) {
        auto& scenario = worldSpec.scenario();
        // Validate the list of user-defined events
        int num = scenario.custom_events_size();
        for (int i = 0; i < num; ++i) {
            auto& name = scenario.custom_events(i).name();
            rrThrowVerify(!name.empty(), "Invalid name for user-defined event. A user-defined event must have a name.");
            auto p = mUserDefinedEventMap.insert({name, {}});
            rrThrowVerify(p.second, "Invalid name for user-defined event " + name +
                                        ". A user-defined "
                                        "event must have an unique name.");
        }

        // Validate the list of user-defined actions
        num = scenario.custom_actions_size();
        for (int i = 0; i < num; ++i) {
            auto& name = scenario.custom_actions(i).name();
            rrThrowVerify(!name.empty(), "Invalid name for user-defined action. A user-defined action must have a name.");
            auto p = mUserDefinedActionList.insert(name);
            rrThrowVerify(p.second, "Invalid name for user-defined action " + name +
                                        ". A user-defined "
                                        "action must have an unique name.");
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<ScenarioExecutor> ScenarioExecutor::CreateScenarioExecutor(
    const sseProto::Actor& worldActor,
    Simulation*            sim) {
    // Use phase diagram executor by default, unless the root activity is defined
    bool useActivityDiagramExecutor =
        worldActor.actor_spec().has_world_spec() &&
        worldActor.actor_spec().world_spec().has_scenario() &&
        worldActor.actor_spec().world_spec().scenario().has_root_activity();
    if (useActivityDiagramExecutor) {
        return sim->GetInstantiator()->CreateActivityDiagramExecutor(worldActor, sim);
    } else {
        return std::make_unique<PhaseDiagramExecutor>(worldActor, sim);
    }
}

////////////////////////////////////////////////////////////////////////////////

void ScenarioExecutor::RegisterPath(const sseProtoCommon::Path* path, const std::string& actorId) {
    rrThrowVerify(!stdx::ContainsKey(mPathMap, actorId),
                  "Attempting to add duplicate path.");
    mPathMap[actorId] = path;
}

////////////////////////////////////////////////////////////////////////////////

const sseProtoCommon::Path* ScenarioExecutor::FindPath(const std::string& actorId) {
    auto path = mPathMap.find(actorId);
    rrThrowVerify(path != mPathMap.end(), "Can't find path");
    return path->second;
}

////////////////////////////////////////////////////////////////////////////////

rrOpt<sseProto::CustomCommand> ScenarioExecutor::PopUserDefinedEvent(const std::string& name) {
    rrOpt<sseProto::CustomCommand> retVal;
    auto                           pos = mUserDefinedEventMap.find(name);
    if (pos != mUserDefinedEventMap.end() && !pos->second.empty()) {
        retVal = pos->second.front();
        pos->second.pop_front();
    }
    return retVal;
}

////////////////////////////////////////////////////////////////////////////////

rrOpt<std::deque<sseProto::CustomCommand>> ScenarioExecutor::GetUserDefinedEvents(const std::string& name) {
    rrOpt<std::deque<sseProto::CustomCommand>> retVal;
    auto                                       pos = mUserDefinedEventMap.find(name);
    if (pos != mUserDefinedEventMap.end() && !pos->second.empty()) {
        retVal = pos->second;
    }
    return retVal;
}

////////////////////////////////////////////////////////////////////////////////

bool ScenarioExecutor::IsUserDefinedActionRegistered(const std::string& actionName) const {
    return mUserDefinedActionList.find(actionName) != mUserDefinedActionList.end();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace sse
