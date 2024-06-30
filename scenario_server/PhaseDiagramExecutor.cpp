#include "PhaseDiagramExecutor.h"

#include "Action.h"
#include "Phase.h"
#include "RoadRunnerConversions.h"
#include "Simulation.h"
#include "Utility.h"

using namespace sseProto;

namespace sse {
////////////////////////////////////////////////////////////////////////////////

PhaseDiagramExecutor::PhaseDiagramExecutor(const Actor& worldActor, Simulation* sim)
    : ScenarioExecutor(worldActor, sim) {
    auto& worldSpec = worldActor.actor_spec().world_spec();

    // We allow a world to not have a scenario for testing of partially built
    // model. Actors will keep stationary during simulation, due to the lack of
    // scenario events/commands (e.g. actions, modifiers)
    if (worldSpec.has_scenario() && worldSpec.scenario().has_root_phase()) {
        // Create runtime object for root phase
        mRootPhase = Phase::CreatePhase(this, worldSpec.scenario().root_phase());
    }
}

////////////////////////////////////////////////////////////////////////////////

void PhaseDiagramExecutor::ProcessEvent(const rrSP<sseProto::Event>& evt) {
    // Do nothing for empty scenario
    if (mRootPhase == nullptr) {
        return;
    }

    const bool isUpdateEvt = evt->has_scenario_update_event();
    if (evt->has_simulation_start_event() || isUpdateEvt) {
        // Receive user-defined events of last step
        ReceiveEventsRequest  req;
        ReceiveEventsResponse res;
        mSimulation->ReceiveEvents(req, res);
        int num = res.events_size();
        for (int i = 0; i < num; ++i) {
            const auto& customCommand = res.events(i).user_defined_event();
            mUserDefinedEventMap[customCommand.name()].push_back(customCommand);
        }

        // Update scenario logic after execution of
        // - Simulation start event: for initialization
        // - Simulation step event: for catching up with changes in world states
        do {
            // To be compatible with OSC 1.x and 2.0, SSE assumes the super-step
            // semantics as described in the following Stateflow documentation:
            // https://www.mathworks.com/help/stateflow/ug/super-step-semantics.html
            // This will allow multiple phase state transitions to happen within
            // the same step. The scenario logic model is in a stable status at
            // the end of each step, in term that no feasible phase state
            // transition can further happen.
            mHasPhaseStateTransition = false;
            mRootPhase->Update();
        } while (isUpdateEvt && mHasPhaseStateTransition);

        // Clean up user-defined event map
        for (auto& it : mUserDefinedEventMap) {
            it.second.clear();
        }

        // Dispatch actor action events to simulation clients
        for (auto& [id, p] : mPhasesToDispatch) {
            auto proto = p->GetUnskippedActions();
            if (proto.action_phase().actions_size() > 0) {
                rrSP<Event> actionEvt = EventCalendar::CreateActionEvent(proto);
                mSimulation->PublishEvent(actionEvt);
                mPhasesInProcess.insert({id, p});
                p->NotifyActionEventDispatched();
            }
        }
        mPhasesToDispatch.clear();
    }
}

////////////////////////////////////////////////////////////////////////////////

void PhaseDiagramExecutor::NotifyActionComplete(const std::string& actionId) {
    std::unique_lock lock(mActionCompletedMutex);
    for (auto& it : mPhasesInProcess) {
        auto action = it.second->FindFinalAction(actionId);
        if (action != nullptr) {
            action->Complete(this);
            return;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void PhaseDiagramExecutor::GetPhaseStatus(sseProto::GetPhaseStatusResponse& res) {
    for (const auto& phase : mIdToPhaseMap) {
        phase.second->GetPhaseStatus(res.add_phase_status());
    }
}

////////////////////////////////////////////////////////////////////////////////

void PhaseDiagramExecutor::RegisterPhase(const rrSP<Phase>& phase, const std::string& id) {
    rrThrowVerify(!stdx::ContainsKey(mIdToPhaseMap, id),
                  "Attempting to add duplicate phase (Id : " + id + ").");
    mIdToPhaseMap[id] = phase;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Phase> PhaseDiagramExecutor::FindPhase(const std::string& phaseId) const {
    auto pos = mIdToPhaseMap.find(phaseId);
    rrThrowVerify(pos != mIdToPhaseMap.end(),
                  "Invalid phase with identifier:  " + phaseId);
    return pos->second;
}

////////////////////////////////////////////////////////////////////////////////

void PhaseDiagramExecutor::NotifyActorActionPhaseStarted(const ActorActionPhase* newPhase) {
    // Detect and resolve conflicts
    // Process the list of phases to dispatch
    for (const auto& it : mPhasesToDispatch) {
        it.second->DetectAndResolveConflict(newPhase);
    }
    // Add new phase to this list
    mPhasesToDispatch.insert({newPhase->GetDataModel().id(), newPhase});
    // Process the list of phases in process
    for (const auto& it : mPhasesInProcess) {
        it.second->DetectAndResolveConflict(newPhase);
    }
}

////////////////////////////////////////////////////////////////////////////////

void PhaseDiagramExecutor::NotifyActorActionPhaseEnded(const ActorActionPhase* phase) {
    const auto& phaseId = phase->GetDataModel().id();
    mPhasesToDispatch.erase(phaseId);
    mPhasesInProcess.erase(phaseId);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace sse
