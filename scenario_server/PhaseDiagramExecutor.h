#pragma once

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "mw_grpc_clients/scenario_server/ScenarioExecutor.hpp"

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
// Class PhaseDiagramExecutor implements a run-time scenario that are represented
// as a behavior tree (or hierarchical state machine). Nodes of the behavior
// tree are run-time phases of the scenario.
//
class PhaseDiagramExecutor : public ScenarioExecutor {
  public:
    // Non-default constructor of an scenario executor object.
    // Inputs:
    // - worldActor: The top-level, world actor that contains the scenario
    //               that this executor shall execute
    // - sim:        The Simulation object that owns this scenario executor.
    //               A simulation uniquely owns a scenario executor. Through
    //               the executor, scenario logic will be continuously evaluated
    //               throughout the simulation.
    PhaseDiagramExecutor(const sseProto::Actor& worldActor, Simulation* sim);

    void ProcessEvent(const std::shared_ptr<sseProto::Event>& evt) override;
    void NotifyActionComplete(const std::string& actionId) override;

    std::shared_ptr<Phase> GetRootPhase() const {
        return mRootPhase;
    }

    void NotifyActorActionPhaseStarted(const ActorActionPhase* phase);
    void NotifyActorActionPhaseEnded(const ActorActionPhase* phase);

    void NotifyPhaseStateTransition() {
        mHasPhaseStateTransition = true;
    }

    void                   RegisterPhase(const std::shared_ptr<Phase>& phase, const std::string& id);
    void                   GetPhaseStatus(sseProto::GetPhaseStatusResponse& res);
    std::shared_ptr<Phase> FindPhase(const std::string& phaseId) const;

  private:
    // Top-level phase of this scenario
    std::shared_ptr<Phase> mRootPhase = nullptr;

    // A Boolean flag to indicate if any state transition has happened during the
    // last update of the root phase. The state transition may happen on any
    // descendant phase of the root phase.
    bool mHasPhaseStateTransition = false;

    // Contains identity of all phases of this scenario
    std::map<std::string, std::shared_ptr<Phase>> mIdToPhaseMap;

    // Contains actor action phases that have started in the current step, but
    // not yet dispatched to simulation clients. Use phase Id as key for look up.
    std::map<std::string, const ActorActionPhase*> mPhasesToDispatch;

    // Contains actor action phases that have started and dispatched, but not
    // yet ended. Use phase Id as key for look up.
    std::map<std::string, const ActorActionPhase*> mPhasesInProcess;

    // Mutex for modifying actions that are known to be completed in the last step
    mutable std::shared_mutex mActionCompletedMutex;
};
} // namespace sse
