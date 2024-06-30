#pragma once

#include "mw_grpc_clients/mw_grpc_clients_spec.hpp"
#include "ProtobufNamespace.hpp"

#include <mathworks/scenario/simulation/scenario.pb.h>
#include <mathworks/scenario/simulation/cosim.pb.h>

#include <deque>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace sse {
class Simulation;
class Phase;
class ActorActionPhase;

////////////////////////////////////////////////////////////////////////////////
// Class ScenarioExecutor is the base class for all types of scenario-logic
// executors. Derived classes include PhaseDiagramExeuctor and
// ActivityDiagramExecutor that execute phase diagrams and activity diagrams
// respectively.
//
class MW_GRPC_CLIENTS_EXPORT_CLASS ScenarioExecutor {
  public:
    // Non-default constructor of an scenario executor object.
    // Inputs:
    // - worldActor: The top-level, world actor that contains the scenario
    //               that this executor shall execute
    // - sim:        The Simulation object that owns this scenario executor.
    //               A simulation uniquely owns a scenario executor. Through
    //               the executor, scenario logic will be continuously evaluated
    //               throughout the simulation.
    ScenarioExecutor(const sseProto::Actor& worldActor, Simulation* sim);
    ScenarioExecutor()          = default;
    virtual ~ScenarioExecutor() = default;

    // Factory method for creating a specific type of scenario executor
    static std::unique_ptr<ScenarioExecutor> CreateScenarioExecutor(
        const sseProto::Actor& worldActor,
        Simulation*            sim);

    virtual void ProcessEvent(const std::shared_ptr<sseProto::Event>& evt) = 0;
    virtual void NotifyActionComplete(const std::string& actionId)         = 0;

    Simulation* GetSimulation() const {
        return mSimulation;
    }

    void                        RegisterPath(const sseProtoCommon::Path* path, const std::string& actorId);
    const sseProtoCommon::Path* FindPath(const std::string& actorId);

    bool IsUserDefinedEventRegistered(const std::string& /*name*/) const {
        return true;
        //? TODO: temporarily disable event declaration check
        // - Enable this check with Catalog framework, i.e. Events are declared in Catalog
        // return (mCustomCommands.find(name) != mCustomCommands.end());
    }

    std::optional<sseProto::CustomCommand>             PopUserDefinedEvent(const std::string& name);
    std::optional<std::deque<sseProto::CustomCommand>> GetUserDefinedEvents(const std::string& name);

    bool IsUserDefinedActionRegistered(const std::string& actionName) const;

  protected:
    // Simulation run that owns this scenario executor.
    // For convenient, a pointer to the simulation run is kept as a reference.
    Simulation* const mSimulation = nullptr;

    // Map for storing unprocessed custom events
    // - All events must have unique name
    std::unordered_map<std::string, std::deque<sseProto::CustomCommand>> mUserDefinedEventMap;

    // Map for storing registered user-defined action
    // - All actions must have unique name
    std::unordered_set<std::string> mUserDefinedActionList;

    // Map for fast look-up from an actor id to path in follow-path driving mode
    std::unordered_map<std::string, const sseProtoCommon::Path*> mPathMap;
};

} // namespace sse
