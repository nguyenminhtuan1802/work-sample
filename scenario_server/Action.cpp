#include "mw_grpc_clients/scenario_server/ScenarioExecutor.hpp"

#include "Action.h"
#include "Condition.h"
#include "EventCalendar.h"
#include "Simulation.h"
#include "Utility.h"
#include "Phase.h"

using namespace sseProto;

namespace sse {
////////////////////////////////////////////////////////////////////////////////
// Action
////////////////////////////////////////////////////////////////////////////////

Action::Action(Phase* phase, const sseProto::Action& proto)
    : mPhase(phase)
    , mDataModel(proto) {
    rrThrowVerify(!proto.id().empty(),
                  "Invalid data model for action. Action must have an id.");
}

////////////////////////////////////////////////////////////////////////////////

void Action::Enforce(
    ScenarioExecutor* executor,
    sseProto::Actor&  actor,
    const std::vector<rrSP<Action>>& /*phaseActions*/) const {
    if (mCondition) {
        mCondition->Enforce(executor, actor);
    }
}

////////////////////////////////////////////////////////////////////////////////

void Action::Apply(ScenarioExecutor* executor) {
    if (mCondition) {
        mCondition->Apply(executor);
    }
}

////////////////////////////////////////////////////////////////////////////////

bool Action::CheckStatus(ScenarioExecutor* executor) {
    if (mCondition) {
        return mCondition->Check(executor);
    } else {
        return true;
    }
}

////////////////////////////////////////////////////////////////////////////////

void Action::Preempt(ScenarioExecutor* executor) {
    switch (mEventStatus) {
    case ACTION_EVENT_STATUS_UNSPECIFIED:
        // If the action has not yet started, set its status as skipped
        mEventStatus = ACTION_EVENT_STATUS_SKIPPED;
        break;
    case ACTION_EVENT_STATUS_DISPATCHED:
        // If the action has already been dispatched, set its status as interrupted
        mEventStatus = ACTION_EVENT_STATUS_INTERRUPTED;
        NotifyActionComplete(executor, mEventStatus);
        break;
    case ACTION_EVENT_STATUS_INTERRUPTED:
    case ACTION_EVENT_STATUS_SKIPPED:
    case ACTION_EVENT_STATUS_DONE:
    default:
        // Do nothing if the action is already in one of the completed states
        break;
    }
}

void Action::Complete(ScenarioExecutor* executor) {
    switch (mEventStatus) {
    case ACTION_EVENT_STATUS_UNSPECIFIED:
        // Must not reach here: must not complete an action that has not yet dispatched
        rrThrow("Internal error: invalid event status for a action: " + mDataModel.id());
    case ACTION_EVENT_STATUS_DISPATCHED:
        // If the action is in the dispatched state, transit it into the done state
        // and notify simulation clients via an action complete event
        mEventStatus = ACTION_EVENT_STATUS_DONE;
        NotifyActionComplete(executor, mEventStatus);
        break;
    case ACTION_EVENT_STATUS_INTERRUPTED:
    case ACTION_EVENT_STATUS_SKIPPED:
    case ACTION_EVENT_STATUS_DONE:
    default:
        // Do nothing if the action is already in one of the completed states
        break;
    }
}

////////////////////////////////////////////////////////////////////////////////

void Action::GetActionStatus(sseProto::ActionStatus* rtAction) {
    rtAction->set_id(mDataModel.id());
    rtAction->set_action_event_status(mEventStatus);
    if (mCondition) {
        mCondition->GetConditionStatus(rtAction->mutable_condition_status());
    }
}

////////////////////////////////////////////////////////////////////////////////
// ActorAction
////////////////////////////////////////////////////////////////////////////////

bool ActorAction::InConflictWith(const ActorAction& other) const {
    // Return false if two actions are for different actor
    if (mActorId != other.mActorId) {
        return false;
    }

    // Return true if the other action controls a dimension that is same with
    // this action.
    auto                          d1 = GetControlDimensions();
    auto                          d2 = other.GetControlDimensions();
    std::vector<ControlDimension> common;

    std::sort(d1.begin(), d1.end());
    std::sort(d2.begin(), d2.end());

    std::set_intersection(d1.begin(), d1.end(), d2.begin(), d2.end(), back_inserter(common));

    return !common.empty();
}

////////////////////////////////////////////////////////////////////////////////

void ActorAction::NotifyActionComplete(ScenarioExecutor*           executor,
                                       sseProto::ActionEventStatus status) const {
    rrSP<Event> actionCompleteEvt = EventCalendar::CreateActionCompleteEvent(
        mDataModel.id(), mActorId, status);
    executor->GetSimulation()->PublishEvent(actionCompleteEvt);
}

////////////////////////////////////////////////////////////////////////////////
// LaneChangeAction
////////////////////////////////////////////////////////////////////////////////

LaneChangeAction::LaneChangeAction(Phase* phase, const std::string& actor, const sseProto::Action& proto)
    : ActorAction(phase, actor, proto) {
    const sseProto::ActorAction& am = proto.actor_action();
    rrThrowVerify(am.has_lane_change_action(),
                  "Invalid data model for action: " + proto.id());

    // Get min and max target lane numbers
    const sseProto::LaneChangeTarget& laneTarget = am.lane_change_action().lane_change_target();
    const LaneReference&              ref        = laneTarget.lane_reference();
    LaneComparison                    refType    = ref.lane_comparison();
    // For same-as comparison relative to a reference vehicle, the min and max number
    // of lane difference are both zero
    int min = 0;
    int max = 0;
    // For other types of comparison, set the min, max values based on the lane modifier
    if (refType != LANE_COMPARISON_SAME_AS) {
        if (laneTarget.has_range()) {
            min = laneTarget.range().min();
            max = laneTarget.range().max();
        } else {
            min = laneTarget.value();
            max = min;
        }
    }
    // Create the run-time lane change condition object
    mCondition = std::make_unique<ActorLaneCondition>(mActorId, min, max, ref);
}

////////////////////////////////////////////////////////////////////////////////

void LaneChangeAction::AddDependency(std::vector<std::string>& dependencies) const {
    mCondition->AddDependency(dependencies);
}

////////////////////////////////////////////////////////////////////////////////
// LateralOffsetAction
////////////////////////////////////////////////////////////////////////////////

LateralOffsetAction::LateralOffsetAction(Phase* phase, const std::string& actor, const sseProto::Action& proto)
    : ActorAction(phase, actor, proto) {
    const sseProto::ActorAction& am = proto.actor_action();
    rrThrowVerify(am.has_lateral_offset_action(),
                  "Invalid data model for action: " + proto.id());

    const sseProto::LateralOffsetAction& lateralOffsetAction = am.lateral_offset_action();

    // Create the run-time lateral offset condition object
    mCondition = std::make_unique<ActorLateralOffsetCondition>(mActorId, lateralOffsetAction.lateral_offset_target().offset_value());
}

////////////////////////////////////////////////////////////////////////////////

void LateralOffsetAction::AddDependency(std::vector<std::string>& dependencies) const {
    mCondition->AddDependency(dependencies);
}

////////////////////////////////////////////////////////////////////////////////
// PositionAction
////////////////////////////////////////////////////////////////////////////////

bool PositionAction::CheckStatus(ScenarioExecutor*) {
    rrThrow("Not implemented yet.");
}

////////////////////////////////////////////////////////////////////////////////
// SpeedAction
////////////////////////////////////////////////////////////////////////////////

SpeedAction::SpeedAction(Phase* phase, const std::string& actor, const sseProto::Action& proto)
    : ActorAction(phase, actor, proto) {
    const sseProto::ActorAction& am = proto.actor_action();
    rrThrowVerify(am.has_speed_action(),
                  "Invalid data model for action: " + proto.id());

    // Create the run-time actor speed condition object
    const auto& speedTarget     = am.speed_action().speed_target();
    const auto& speedComparison = speedTarget.speed_reference().speed_comparison();
    if (speedComparison != SPEED_COMPARISON_FROM_PATH) {
        mCondition = std::make_unique<ActorSpeedCondition>(mActorId, am.speed_action().speed_target());
    }
}

////////////////////////////////////////////////////////////////////////////////

void SpeedAction::Enforce(
    ScenarioExecutor*                executor,
    sseProto::Actor&                 actor,
    const std::vector<rrSP<Action>>& phaseActions) const {
    // NOTE: This is guaranteed to exist by the constructor
    const auto& speedTarget     = mDataModel.actor_action().speed_action().speed_target();
    const auto& speedComparison = speedTarget.speed_reference().speed_comparison();

    // Actor speed intialization is performed by the SpeedCondition for absolute/relative speed, but handled
    //	here directly for path-based speeds
    //? TODO: Unify these approaches, possibly by performing all speed initialization here (removing Condition::Enforce entirely)
    if (speedComparison != SPEED_COMPARISON_FROM_PATH) {
        ActorAction::Enforce(executor, actor, phaseActions);
    } else {
        // Find path action
        rrSP<Action> pathAction;
        for (const auto& action : phaseActions) {
            if (auto casted = rrDynamicCast<PathAction>(action)) {
                pathAction = casted;
                break;
            }
        }

        rrThrowVerify(pathAction,
                      "Unable to initialize speed action '" + mDataModel.id() + "'. No path action is present.");
        const auto& pathTimings = pathAction->GetDataModel().actor_action().path_action().timings();
        rrThrowVerify(!pathTimings.empty(),
                      "Unable to initialize speed action '" + mDataModel.id() + "'. No path timings are present.");

        // Set initial speed from first timings point
        const double initialSpeed = pathTimings[0].speed();
        SetVelocity(actor, initialSpeed);
    }
}

////////////////////////////////////////////////////////////////////////////////

bool SpeedAction::HasRegularEnding() const {
    // SpeedAction with continuous reference sampling mode does not have regular ending (as in OSC v1.2)
    return (mDataModel.actor_action().speed_action().speed_target().speed_reference().reference_sampling_mode() != ReferenceSamplingMode::REFERENCE_SAMPLING_MODE_CONTINUOUS);
}

////////////////////////////////////////////////////////////////////////////////

void SpeedAction::AddDependency(std::vector<std::string>& dependencies) const {
    if (mCondition) {
        mCondition->AddDependency(dependencies);
    }
}

////////////////////////////////////////////////////////////////////////////////
// LongitudinalDistanceAction
////////////////////////////////////////////////////////////////////////////////

LongitudinalDistanceAction::LongitudinalDistanceAction(Phase* phase, const std::string& actor, const sseProto::Action& proto)
    : ActorAction(phase, actor, proto) {
    const sseProto::ActorAction& am = proto.actor_action();
    rrThrowVerify(am.has_longitudinal_distance_action(),
                  "Invalid data model for action: " + proto.id());

    // Create the run-time longitudinal distance condition object
    const sseProto::LongitudinalDistanceAction& action = am.longitudinal_distance_action();
    rrThrowVerify(action.has_distance_target() && action.distance_target().distance_dimension_type() == DISTANCE_DIMENSION_TYPE_LONGITUDINAL,
                  "Invalid data model for action: " + proto.id());
    const auto& distanceTarget = action.distance_target();
    rrThrowVerify(distanceTarget.has_distance_reference(),
                  "Invalid data model for action: " + proto.id());
    const auto& distanceReference = distanceTarget.distance_reference();
    rrThrowVerify(distanceReference.reference_type_case() == sseProto::DistanceReference::ReferenceTypeCase::kReferenceActorId,
                  "Invalid data model for action: " + proto.id());

    mCondition = std::make_unique<LongitudinalDistanceCondition>(mActorId, distanceTarget);
}

////////////////////////////////////////////////////////////////////////////////

bool LongitudinalDistanceAction::HasRegularEnding() const {
    // LongitudinalDistanceAction with continuous reference sampling mode does not have regular ending (as in OSC v1.2)
    return (mDataModel.actor_action().longitudinal_distance_action().reference_sampling_mode() !=
            ReferenceSamplingMode::REFERENCE_SAMPLING_MODE_CONTINUOUS);
}

////////////////////////////////////////////////////////////////////////////////

void LongitudinalDistanceAction::AddDependency(std::vector<std::string>& dependencies) const {
    if (mCondition) {
        mCondition->AddDependency(dependencies);
    }
}

////////////////////////////////////////////////////////////////////////////////
// ChangeParameterAction
////////////////////////////////////////////////////////////////////////////////

bool ChangeParameterAction::InConflictWith(const ActorAction& other) const {
    // In conflict when two actions are changing the same parameter
    const auto& otherAction = other.GetDataModel().actor_action();
    if (otherAction.has_change_parameter_action()) {
        auto& n1 = mDataModel.actor_action().change_parameter_action().parameter().name();
        auto& n2 = otherAction.change_parameter_action().parameter().name();
        return (n1 == n2);
    } else {
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////////

void ChangeParameterAction::Apply(ScenarioExecutor* executor) {
    auto actorPtr = executor->GetSimulation()->FindActor(mActorId);

    auto updateParameter = [](google::protobuf::RepeatedPtrField<sseProto::Attribute>& parameterCollection,
                              const sseProto::Attribute&                               parameter) {
        for (int idx = 0; idx < parameterCollection.size(); ++idx) {
            if (parameterCollection[idx].name() == parameter.name()) {
                parameterCollection.at(idx).CopyFrom(parameter);
                break;
            }
        }
    };

    switch (mDataModel.actor_action().change_parameter_action().parameter_type()) {
    case sseProto::PARAMETER_TYPE_ACTOR:
        updateParameter(*actorPtr->mutable_actor_runtime()->mutable_parameters(),
                        mDataModel.actor_action().change_parameter_action().parameter());
        break;
    case sseProto::PARAMETER_TYPE_BEHAVIOR:
        updateParameter(*actorPtr->mutable_actor_runtime()->mutable_behavior_parameters(),
                        mDataModel.actor_action().change_parameter_action().parameter());
        break;
    default:
        break;
    }
}

////////////////////////////////////////////////////////////////////////////////
// UserDefinedAction
////////////////////////////////////////////////////////////////////////////////

void UserDefinedAction::Apply(ScenarioExecutor* executor) {
    const auto& customCommand = mDataModel.actor_action().user_defined_action().custom_command();
    rrThrowVerify(executor->IsUserDefinedActionRegistered(customCommand.name()),
                  "Undefined user-defined action in action: " + mDataModel.id());
}

////////////////////////////////////////////////////////////////////////////////
// ScenarioSuccess
////////////////////////////////////////////////////////////////////////////////

void ScenarioSuccess::Apply(ScenarioExecutor*) {
    ScenarioSuccessException exp;
    exp.mSummary = "Scenario success has been reported by: " + mPhase->GetDataModel().id();

    // Copy over runtime conditions that trigger the scenario success
    auto successCondition = mPhase->GetStartCondition();
    if (successCondition) {
        ConditionStatus rtCondition;
        successCondition->GetConditionStatus(&rtCondition);
        *(exp.mSuccessStatus.add_status()) = rtCondition;
        exp.mSummary                       = exp.mSummary + "\n" + successCondition->GetSummary();
    }
    // exp.mSummary = FormatHtmlMultilineString(exp.mSummary);
    throw exp;
}

////////////////////////////////////////////////////////////////////////////////
// ScenarioFailure
////////////////////////////////////////////////////////////////////////////////

void ScenarioFailure::Apply(ScenarioExecutor*) {
    ScenarioFailureException exp;
    exp.mSummary = "Simulation failed: ";

    // Copy over runtime conditions that trigger the scenario failure
    auto failureCondition = mPhase->GetStartCondition();
    if (failureCondition) {
        ConditionStatus rtCondition;
        failureCondition->GetConditionStatus(&rtCondition);
        if (rtCondition.condition_state() == ConditionState::CONDITION_STATE_SATISFIED) {
            *(exp.mFailureStatus.add_status()) = rtCondition;
            exp.mSummary                       = exp.mSummary + failureCondition->GetSummary();
        }
    }
    // exp.mSummary = FormatHtmlMultilineString(exp.mSummary);
    throw exp;
}

////////////////////////////////////////////////////////////////////////////////
// EmitAction
////////////////////////////////////////////////////////////////////////////////

void EmitAction::Apply(ScenarioExecutor* executor) {
    const auto& customCommand = mDataModel.system_action().send_event_action().custom_command();
    rrThrowVerify(executor->IsUserDefinedEventRegistered(customCommand.name()),
                  "Undefined user-defined event in action: " + mDataModel.id());

    SendEventsRequest  req;
    SendEventsResponse res;
    auto*              evnt = req.add_events();

    // Events sending from scenario logic has world actor as sender ID
    evnt->set_sender_id(Simulation::cWorldActorID);
    evnt->mutable_user_defined_event()->CopyFrom(customCommand);
    auto result = executor->GetSimulation()->SendEvents(req, res);
    if (result.Type == Result::EnumType::eError) {
        rrThrow(result.Message);
    }
}

////////////////////////////////////////////////////////////////////////////////
} // namespace sse
