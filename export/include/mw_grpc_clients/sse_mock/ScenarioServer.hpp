// Copyright 2021-2023 The MathWorks, Inc.
#pragma once

#include "mw_grpc_clients/mw_grpc_clients_spec.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Keeping consistency with road runner notation
template <typename T>
using rrUP = std::unique_ptr<T>;

////////////////////////////////////////////////////////////////////////////////
// gRPC server class for simulation management. This class in conjunction with
// EventPublisher, keeps track of simulation clients and publishes simulation events
// such as, simulation start, pause, stop etc.

// Forward declare protos
namespace mathworks {
namespace scenario {
namespace simulation {
class Actor;
class SimulationSettings;
}}} // namespace mathworks::scenario::simulation
namespace mathworksScenario = mathworks::scenario::simulation;

namespace sse_mock {

class ScenarioCosimApi;
struct EventCounts;

class MW_GRPC_CLIENTS_EXPORT_CLASS ScenarioServer final {
  public:
    ScenarioServer();
    ScenarioServer(const std::string connectionAddress);
    ScenarioServer(const ScenarioServer&)            = delete;
    ScenarioServer& operator=(const ScenarioServer&) = delete;
    ~ScenarioServer();

    // Pre-defined event priority for events.
    static const int cEventPriorityClientServer;
    static const int cEventPriorityNotifyChanges;
    static const int cEventPrioritySimulationStart;
    static const int cEventPrioritySimulationStop;
    static const int cEventPrioritySimulationStep;
    static const int cEventPrioritySimulationPostStep;
    static const int cEventPriorityScenarioLogic;

    static const std::string matlabClientID;


    mathworksScenario::Actor* getWorldActor();

    std::string getPortNumber();

    void BroadcastClientSubscribedEvent(const std::string& client_id);

    void BroadcastSimulationStartEvent();

    void BroadcastServerShutdownEvent();

    void BroadcastCreateActorEvent();
    void BroadcastSimulationStepEvent(double eventT, int numSteps);

    void BroadcastSimulationStopEvent(double eventT, int numSteps);

    void BroadcastUpdateParamEvent(const std::string& actorID,
                                   const std::string& paramName,
                                   const std::string& paramValue);

    void BroadcastUserDefinedEvent(const std::string&              actorID,
                                   const std::string&              commandName,
                                   const std::vector<std::string>& attributeNames,
                                   const std::vector<std::string>& attributeValues,
                                   bool                            instantaneous);

    void AddBehaviorToWorld(const std::string& behaviorID,
                            const std::string& behaviorType,
                            const std::string& artifactLocation);

    void AddActorToWorld(const std::string& actorName,
                         const std::string& actorID,
                         const std::string& behaviorID);

    void AddParamToActor(const std::string& actorID,
                         const std::string& paramName,
                         const std::string& paramValue);

    void AddChildToActor(const std::string& parentID, const std::string& childID);

    std::vector<std::string> getActorChildren(const std::string& actorID);

    const std::string getActorParent(const std::string& actorID);

    mathworksScenario::SimulationSettings GetDefaultSimulationSettings();

    // Used for polling
    const EventCounts& getEventCounts() const;

    using ActorMap = std::unordered_map<std::string, rrUP<mathworksScenario::Actor>>;
    const ActorMap& getRtActors() const;

  private:
    std::unique_ptr<mathworksScenario::Actor>   mWorldActor;
    std::unique_ptr<sse_mock::ScenarioCosimApi> mCosimApi;

    // Contains all the actors for reference during simulation
    ActorMap RtActors;

    std::string serverAddress;
};
} // namespace sse_mock

////////////////////////////////////////////////////////////////////////////////

// LocalWords:  protos
