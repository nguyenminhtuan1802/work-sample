#include "EventCalendar.h"
#include "Utility.h"

using namespace sseProto;

namespace sse {

////////////////////////////////////////////////////////////////////////////////
// Create events for client-server management
////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateServerShutdownEvent() {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPriorityClientServer);
    evt->set_need_set_ready(false);
    evt->mutable_server_shutdown_event();
    return evt;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateClientSubscribedEvent(const std::string& client_id) {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPriorityClientServer);
    evt->set_need_set_ready(false);

    auto* subEvt = evt->mutable_client_subscribed_event();
    subEvt->set_client_id(client_id);

    return evt;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateClientUnsubscribedEvent(const std::string& client_id) {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPriorityClientServer);
    evt->set_need_set_ready(false);

    auto* unsubEvt = evt->mutable_client_unsubscribed_event();
    unsubEvt->set_client_id(client_id);

    return evt;
}

////////////////////////////////////////////////////////////////////////////////
// Create events for notifying changes in scene, scenario, simulation
// settings, and simulation runtime status
////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateSceneChangedEvent() {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPriorityNotifyChanges);
    evt->set_need_set_ready(false);
    evt->mutable_scene_changed_event();
    return evt;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateScenarioChangedEvent() {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPriorityNotifyChanges);
    evt->set_need_set_ready(false);
    evt->mutable_scenario_changed_event();
    return evt;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateSimulationSettingsChangedEvent(const SimulationSettings& settings) {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPriorityNotifyChanges);
    evt->set_need_set_ready(false);

    auto* e                             = evt->mutable_simulation_settings_changed_event();
    *(e->mutable_simulation_settings()) = settings;

    return evt;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateSimulationStatusChangedEvent(SimulationStatus status) {
    rrSP<Event> evt = std::make_shared<Event>();
    // A simulation status changed event has the same priority with a scenario
    // changed event
    evt->set_priority(cEventPriorityNotifyChanges);
    evt->set_need_set_ready(false);

    SimulationStatusChangedEvent* e = evt->mutable_simulation_status_changed_event();
    e->set_simulation_status(status);

    return evt;
}

////////////////////////////////////////////////////////////////////////////////
// Create co-simulation events
////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateSimulationStartEvent(bool isReplayMode) {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPrioritySimulationStart);
    evt->set_need_set_ready(true);
    auto startEvent = evt->mutable_simulation_start_event();
    if (isReplayMode) {
        startEvent->set_simulation_mode(SIMULATION_START_MODE_REPLAY);
    }
    return evt;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateSimulationStopEvent(
    double                     eventT,
    int                        numSteps,
    const SimulationStopCause& cause) {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPrioritySimulationStop);
    evt->set_need_set_ready(true);

    SimulationStopEvent* e = evt->mutable_simulation_stop_event();
    e->set_stop_time_seconds(eventT);
    e->set_steps(numSteps);
    *(e->mutable_cause()) = cause;

    return evt;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateSimulationStepEvent(double eventT, int numSteps) {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPrioritySimulationStep);
    evt->set_need_set_ready(true);

    SimulationStepEvent* e = evt->mutable_simulation_step_event();
    e->set_elapsed_time_seconds(eventT);
    e->set_steps(numSteps);

    return evt;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateSimulationPostStepEvent() {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPrioritySimulationPostStep);
    evt->set_need_set_ready(true);
    evt->mutable_simulation_post_step_event();
    return evt;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<sseProto::Event> EventCalendar::CreateScenarioUpdateEvent() {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPriorityScenarioUpdate);
    evt->set_need_set_ready(false);
    evt->mutable_scenario_update_event();
    return evt;
}

////////////////////////////////////////////////////////////////////////////////
// Create scenario custom commands
////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateCreateActorEvent(const Actor&                    actor,
                                                  const std::vector<rrSP<Actor>>& descendants,
                                                  sseProto::Phase*                initPhase) {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPriorityScenarioLogic);
    evt->set_need_set_ready(true);

    CreateActorEvent* e        = evt->mutable_create_actor_event();
    Actor*            newActor = e->mutable_actor();
    *(newActor)                = actor;
    for (const auto& descendant : descendants) {
        e->add_descendants()->CopyFrom(*descendant);
    }
    if (initPhase) {
        e->mutable_initial_phase()->CopyFrom(*initPhase);
    }

    return evt;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateActionEvent(const sseProto::Phase& phase) {
    rrThrowVerify(phase.has_action_phase() && phase.action_phase().has_actor_action_phase(),
                  "Invalid data model for an action phase.");
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPriorityScenarioLogic);
    evt->set_need_set_ready(false);

    ActionEvent* e        = evt->mutable_action_event();
    *(e->mutable_phase()) = phase;

    return evt;
}

rrSP<sseProto::Event> EventCalendar::CreateActionEvent(const google::protobuf::mathworks::RepeatedPtrField<sseProto::Action>& actions) {
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPriorityScenarioLogic);
    evt->set_need_set_ready(false);

    ActionEvent* e        = evt->mutable_action_event();
    *(e->mutable_actions()) = actions;

    return evt;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Event> EventCalendar::CreateActionCompleteEvent(const std::string& actionId,
                                                     const std::string& actorId,
                                                     ActionEventStatus  finalStatus) {
    rrThrowVerify(finalStatus == ACTION_EVENT_STATUS_DONE || finalStatus == ACTION_EVENT_STATUS_INTERRUPTED,
                  "Invalid final status for a: " + actionId);
    rrSP<Event> evt = std::make_shared<Event>();
    evt->set_priority(cEventPriorityScenarioLogic);
    evt->set_need_set_ready(false);

    auto* e = evt->mutable_action_complete_event();
    e->set_action_id(actionId);
    e->set_actor_id(actorId);
    e->set_final_status(finalStatus);

    return evt;
}

////////////////////////////////////////////////////////////////////////////////
} // namespace sse
