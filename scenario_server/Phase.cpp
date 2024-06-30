#include "Phase.h"
#include "EventCalendar.h"
#include "PhaseDiagramExecutor.h"
#include "Simulation.h"
#include "Utility.h"
#include "Condition.h"
#include "Action.h"
#include "RaceTrack.h"

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"

namespace sse {
////////////////////////////////////////////////////////////////////////////////
// Static phase factory that create a run-time sse::Phase object from a
// sseProto::Phase SSD data model.
//
rrSP<Phase> Phase::CreatePhase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto) {
    rrSP<Phase> phase;
    switch (proto.type_case()) {
    case sseProto::Phase::TypeCase::kCompositePhase:
        switch (proto.composite_phase().type_case()) {
        case sseProto::CompositePhase::TypeCase::kSerialPhase:
            phase = std::make_shared<SerialPhase>(executor, proto);
            break;
        case sseProto::CompositePhase::TypeCase::kMixPhase:
            phase = std::make_shared<MixPhase>(executor, proto);
            break;
        default:
            rrThrow("Unknown type in a composite phase data model.");
        }
        break;
    case sseProto::Phase::TypeCase::kActionPhase:
        switch (proto.action_phase().type_case()) {
        case sseProto::ActionPhase::TypeCase::kActorActionPhase:
            phase = std::make_shared<ActorActionPhase>(executor, proto);
            break;
        case sseProto::ActionPhase::TypeCase::kSystemActionPhase:
            phase = std::make_shared<SystemActionPhase>(executor, proto);
            break;
        default:
            rrThrow("Unknown type in an action phase data model.");
        }
        break;
    default:
        rrThrow("Unknown type in a phase data model.");
    }

    executor->RegisterPhase(phase, proto.id());
    return phase;
}

////////////////////////////////////////////////////////////////////////////////
// Phase
////////////////////////////////////////////////////////////////////////////////

Phase::Phase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto)
    : mExecutor(executor)
    , mDataModel(proto) {
    // Validate a phase has an id
    rrThrowVerify(!proto.id().empty(),
                  "Invalid data model for " + proto.id() + ". Phase must have an id.");

    // Create start condition if specified
    if (proto.has_start_condition()) {
        mStartCondition = Condition::CreateCondition(proto.start_condition());
    }

    // Create end condition if specified
    if (proto.has_end_condition()) {
        mEndCondition = Condition::CreateCondition(proto.end_condition());
    }

    // Create zero-time operations
    auto validateZeroTimeOperations = [](const sseProto::Phase& proto) {
        rrThrowVerify(proto.has_action_phase(),
                      "Invalid data model for a system action phase.");
        int num = proto.action_phase().actions_size();
        for (int i = 0; i < num; ++i) {
            const sseProto::Action& action = proto.action_phase().actions(i);
            // Error out for any action that is not an zero-time operation
            rrThrowVerify(action.has_system_action() && !action.system_action().has_wait_action(),
                          "Invalid data model for a system action phase. All actions of this phase must be zero-time operations.");
        }
    };

    int num = proto.system_actions_size();
    for (int i = 0; i < num; ++i) {
        const auto& sysActionPhase = proto.system_actions(i);
        validateZeroTimeOperations(sysActionPhase);
        auto phase = CreatePhase(executor, sysActionPhase);
        mSystemActions.push_back(phase);
        phase->SetParent(this);
    }
}

////////////////////////////////////////////////////////////////////////////////

bool Phase::Update() {
    bool changeState = false;
    do {
        switch (mState) {
        case State::eIdle:
            changeState = true;
            OnStart();
            break;
        case State::eStart:
            changeState = DoStartUpdate();
            if (changeState) {
                OnRun();
            }
            break;
        case State::eRun:
            changeState = DoRunUpdate();
            if (changeState) {
                OnEnd();
            }
            break;
        case State::eEnd:
            changeState = false;
            // Do nothing
            break;
        }
        if (changeState) {
            mExecutor->NotifyPhaseStateTransition();
        }
    } while (changeState);

    return (mState == State::eEnd);
}

////////////////////////////////////////////////////////////////////////////////

void Phase::OnStart() {
    // Change state to 'starting'
    mState = State::eStart;

    // Apply any start condition
    if (mStartCondition) {
        mStartCondition->Apply(mExecutor);
    }
}

////////////////////////////////////////////////////////////////////////////////

void Phase::OnRun() {
    // Change state to 'running'
    mState = State::eRun;

    // Apply any end condition
    if (mEndCondition) {
        mEndCondition->Apply(mExecutor);
    }
}

////////////////////////////////////////////////////////////////////////////////

void Phase::OnEnd() {
    mState = State::eEnd;

    if (mExecutor->GetRootPhase().get() == this) {
        // The scenario has ended, notify scenario engine to stop simulation
        ScenarioSuccessException exp;
        exp.mSummary = "Simulation ended: ";

        // Copy over any runtime condition that ends the scenario
        auto endCondition = GetEndCondition();
        if (endCondition) {
            sseProto::ConditionStatus rtCondition;
            endCondition->GetConditionStatus(&rtCondition);
            *(exp.mSuccessStatus.add_status()) = rtCondition;
            exp.mSummary                       = exp.mSummary + endCondition->GetSummary();
        }

        if (!HasCompleted()) {
            exp.mSummary += " WARNING: Not all phases completed by end of simulation.";
        }

        throw exp;
    }
}

////////////////////////////////////////////////////////////////////////////////

bool Phase::DoStartUpdate() {
    // Check whether starting conditions are satisfied
    if (mStartCondition) {
        return (mStartCondition->Check(mExecutor));
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////

bool Phase::DoRunUpdate() {
    // Perform zero-time operations.Note:
    // - This is necessary at each step because a zero-time operation may be
    //   guarded by a condition (e.g. on @collision ScenarioFailure())
    // - For a zero-time operation that already executed. Update it is a no-op
    for (auto& operation : mSystemActions) {
        operation->Update();
    }

    // Early return if this phase contains some actor creation and initialization
    // actions. These actions must be dispatched before we evaluate any end
    // condition.
    if (mIsInitPending) {
        // Reset the flag
        mIsInitPending = false;
        return false;
    }

    // Update action complete status
    UpdatePhaseStatus();

    // If an end condition is specified, check the end condition. Otherwise,
    // check if the phase actions have completed.
    return (mEndCondition) ? mEndCondition->Check(mExecutor) : HasCompleted();
}

////////////////////////////////////////////////////////////////////////////////

void Phase::GetPhaseStatus(sseProto::PhaseStatus* rtPhase) {
    auto GetStateProtoEnum = [&]() -> sseProto::PhaseState {
        switch (mState) {
        case State::eRun:
            return sseProto::PhaseState::PHASE_STATE_RUN;
        case State::eStart:
            return sseProto::PhaseState::PHASE_STATE_START;
        case State::eEnd:
            return sseProto::PhaseState::PHASE_STATE_END;
        case State::eIdle:
            return sseProto::PhaseState::PHASE_STATE_IDLE;
        default:
            return sseProto::PhaseState::PHASE_STATE_UNSPECIFIED;
        }
    };

    rtPhase->set_id(mDataModel.id());
    rtPhase->set_phase_state(GetStateProtoEnum());

    if (mStartCondition) {
        mStartCondition->GetConditionStatus(rtPhase->mutable_start_condition_status());
    }
    if (mEndCondition) {
        mEndCondition->GetConditionStatus(rtPhase->mutable_end_condition_status());
    }
}

////////////////////////////////////////////////////////////////////////////////

void Phase::GetDependencyToRun(std::vector<std::string>& dependencies) const {
    if (mStartCondition) {
        mStartCondition->AddDependency(dependencies);
    }
    if (mEndCondition) {
        // For an end condition
        if (mIsInitPending) {
            // If the end condition is on an initial phase, we only need to
            // consider the dependencies in the apply callback. This is because
            // the end condition will not be evaluated till the initial actions
            // are executed
            mEndCondition->AddDependencyOfApply(dependencies);
        } else {
            // If the end condition is on other phases, dependencies from both
            // apply callback and evaluate callback shall be considered, because
            // the condition will be evaluated as soon as the actions start
            mEndCondition->AddDependency(dependencies);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// CompositePhase
////////////////////////////////////////////////////////////////////////////////

CompositePhase::CompositePhase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto)
    : Phase(executor, proto) {
    rrThrowVerify(proto.has_composite_phase(), "Invalid data model for a composite phase.");

    // Create children
    int num = proto.composite_phase().children_size();
    rrThrowVerify(num > 0, "A composite phase must contain 1 or more phases.");
    for (int i = 0; i < num; ++i) {
        const sseProto::Phase& child      = proto.composite_phase().children(i);
        rrSP<Phase>            childPhase = CreatePhase(executor, child);
        childPhase->SetParent(this);
        mChildren.push_back(childPhase);
    }
}

////////////////////////////////////////////////////////////////////////////////
// MixPhase
////////////////////////////////////////////////////////////////////////////////

void MixPhase::OnEnd() {
    // Notify all the children to transit state and do running actions
    for (auto& child : mChildren) {
        child->OnEnd();
    }
    // Transit parent state and do running actions
    CompositePhase::OnEnd();
}

////////////////////////////////////////////////////////////////////////////////

bool MixPhase::DoRunUpdate() {
    // Generate execution order for children if that has not done
    if (mExecOrder.empty()) {
        RaceTrack raceTrack(mExecutor, this);
        raceTrack.Sort();
    }

    // Update all the children
    mChildrenDone = true;
    for (size_t i : mExecOrder) {
        auto& child = mChildren[i];
        // Promote child phase till it can enter ending state
        bool doneRunning = child->Update();
        if (!doneRunning && mChildrenDone) {
            mChildrenDone = false;
        }
    }

    // Do parent updates
    return CompositePhase::DoRunUpdate();
}

////////////////////////////////////////////////////////////////////////////////
// SerialPhase
////////////////////////////////////////////////////////////////////////////////

void SerialPhase::OnEnd() {
    // Notify all the child phases to end
    for (size_t i = mPosition; i < mChildren.size(); ++i) {
        mChildren[i]->OnEnd();
    }

    // End parent
    CompositePhase::OnEnd();
}

////////////////////////////////////////////////////////////////////////////////

bool SerialPhase::DoRunUpdate() {
    // Update children
    size_t num = mChildren.size();
    while (mPosition < num) {
        rrSP<Phase>& child     = mChildren[mPosition];
        bool         childDone = child->Update();
        if (childDone) {
            // Done with this child go to the next child phase
            ++mPosition;
            if (mPosition == num) {
                mChildrenDone = true;
            }
        } else {
            // Stop from proceeding if the current child phase is not completed
            break;
        }
    }

    // Do parent updates
    return CompositePhase::DoRunUpdate();
}

////////////////////////////////////////////////////////////////////////////////
// ActionPhase
////////////////////////////////////////////////////////////////////////////////

void ActionPhase::GetDependencyToRun(std::vector<std::string>& dependencies) const {
    Phase::GetDependencyToRun(dependencies);

    auto processActions = [&](const std::vector<rrSP<Action>>& actions,
                              std::vector<std::string>&        dependencies) {
        for (const auto& action : actions) {
            action->AddDependency(dependencies);
        }
    };

    processActions(mInitialActions, dependencies);
    processActions(mInvariantActions, dependencies);
    processActions(mFinalActions, dependencies);
}

////////////////////////////////////////////////////////////////////////////////

void ActionPhase::InitializeActor(sseProto::Actor& actor) const {
    // Allow parent object to initialize the actor
    Phase::InitializeActor(actor);

    // Initialize any actor of this phase by enforcing the initial actions
    bool hasInitSpeed = false;
    //? TODO: Reconsider the need to provide phase actions to Action::Enforce.
    //	- This is primarily to enable SpeedAction to access path timings from the PathAction
    //	- NOTE: It would be intuitive to only provide initial actions, but PathAction is currently
    //		added as a 'final' action by the SSD converter (should re-examine that decision as well)
    std::vector<rrSP<Action>> phaseActions;
    stdx::Append(phaseActions, mInitialActions);
    stdx::Append(phaseActions, mFinalActions);

    for (auto& action : mInitialActions) {
        action->Enforce(mExecutor, actor, phaseActions);
        // Search for an 'at start' speed action, which is required for actor
        // initialization
        if (!hasInitSpeed &&
            action->GetDataModel().has_actor_action() &&
            action->GetDataModel().actor_action().has_speed_action() &&
            action->GetDataModel().actor_action().phase_interval() ==
                sseProto::PhaseInterval::PHASE_INTERVAL_AT_START) {
            hasInitSpeed = true;
        }
    }
    // Report an error if the required action is not present
    rrThrowVerify(hasInitSpeed, "Fail to initialize actor: " + actor.actor_spec().id() + " when executing " +
                                    mDataModel.id() + ". An 'at start' speed action must be specified for a phase that initializes an actor.");
}

////////////////////////////////////////////////////////////////////////////////

void ActionPhase::OnRun() {
    Phase::OnRun();

    // Activate initial, invariant, and final actions
    for (auto& action : mInitialActions) {
        action->Apply(mExecutor);
    }
    for (auto& action : mInvariantActions) {
        action->Apply(mExecutor);
    }
    for (auto& action : mFinalActions) {
        action->Apply(mExecutor);
    }

    // Check initial temporal values
    for (auto& action : mInitialActions) {
        if (!action->CheckStatus(mExecutor)) {
            rrWarning("Initial temporal value violation in : " + mDataModel.id());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void ActionPhase::OnEnd() {
    switch (mState) {
    case State::eIdle:
    case State::eStart:
    case State::eRun:
        // Transit the action event into skipped or interrupted state if applicable
        for (auto action : GetFinalActions()) {
            action->Preempt(mExecutor);
        }
        break;
    default:
        break;
    }

    Phase::OnEnd();
}

////////////////////////////////////////////////////////////////////////////////

bool ActionPhase::DoRunUpdate() {
    // Check whether all invariant value conditions are met
    for (auto& action : mInvariantActions) {
        if (!action->CheckStatus(mExecutor)) {
            rrWarning("Invariant temporal value violation in : " + mDataModel.id());
        }
    }

    return Phase::DoRunUpdate();
}

////////////////////////////////////////////////////////////////////////////////

void ActionPhase::UpdatePhaseStatus() {
    for (auto& action : mFinalActions) {
        auto status = action->GetEventStatus();
        if (status == sseProto::ACTION_EVENT_STATUS_DISPATCHED) {
            auto objectiveMet = action->CheckStatus(mExecutor);
            if (objectiveMet && action->HasRegularEnding()) {
                action->Complete(mExecutor);
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void ActionPhase::GetPhaseStatus(sseProto::PhaseStatus* rtPhase) {
    // Get status of the parent object
    Phase::GetPhaseStatus(rtPhase);

    auto IterateActionType = [&](std::vector<rrSP<Action>> actions) {
        for (auto& action : actions) {
            action->GetActionStatus(rtPhase->add_action_status());
        }
    };

    // Get status of actions
    IterateActionType(mInitialActions);
    IterateActionType(mFinalActions);
    IterateActionType(mInvariantActions);
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Action> ActionPhase::FindFinalAction(const std::string& actionId) const {
    for (auto& action : mFinalActions) {
        if (action->GetDataModel().id() == actionId) {
            return action;
        }
    }
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////

bool ActionPhase::HasCompleted() const {
    // Check whether all ending value conditions are met
    for (auto& action : mFinalActions) {
        auto evtStatus = action->GetEventStatus();
        if (!(evtStatus == sseProto::ACTION_EVENT_STATUS_SKIPPED ||
              evtStatus == sseProto::ACTION_EVENT_STATUS_INTERRUPTED ||
              evtStatus == sseProto::ACTION_EVENT_STATUS_DONE)) {
            return false;
        }
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////

void ActionPhase::DispatchFinalActions() const {
    for (auto& action : mFinalActions) {
        if (action->GetEventStatus() == sseProto::ACTION_EVENT_STATUS_UNSPECIFIED) {
            action->Dispatch();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// ActorActionPhase
////////////////////////////////////////////////////////////////////////////////

ActorActionPhase::ActorActionPhase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto)
    : ActionPhase(executor, proto) {
    rrThrowVerify(proto.action_phase().has_actor_action_phase(), "Invalid data model for an actor action phase.");
    mActorId = proto.action_phase().actor_action_phase().actor_id();

    // Categorize an action action based on its interval type
    auto AddAction = [&](const rrSP<Action>& m) {
        const sseProto::Action& md = m->GetDataModel();
        switch (md.actor_action().phase_interval()) {
        case sseProto::PHASE_INTERVAL_AT_START:
            mInitialActions.push_back(m);
            mIsInitPending = true;
            break;
        case sseProto::PHASE_INTERVAL_AT_END:
            mFinalActions.push_back(m);
            break;
        case sseProto::PHASE_INTERVAL_INVARIANT:
            mInvariantActions.push_back(m);
            break;
        default:
            rrThrow("Invalid phase interval for an actor action.");
        }
    };

    // Create run-time actions
    rrSP<Action> rtAction;
    int          num = proto.action_phase().actions_size();
    for (int i = 0; i < num; ++i) {
        const sseProto::Action& action = proto.action_phase().actions(i);
        switch (action.type_case()) {
        case sseProto::Action::TypeCase::kActorAction:
            switch (action.actor_action().type_case()) {
            case sseProto::ActorAction::TypeCase::kLaneChangeAction:
                rtAction.reset(new LaneChangeAction(this, mActorId, action));
                AddAction(rtAction);
                break;
            case sseProto::ActorAction::TypeCase::kLateralOffsetAction:
                rtAction.reset(new LateralOffsetAction(this, mActorId, action));
                AddAction(rtAction);
                break;
            case sseProto::ActorAction::TypeCase::kPathAction:
                rtAction.reset(new PathAction(this, mActorId, action));
                AddAction(rtAction);
                executor->RegisterPath(&action.actor_action().path_action().path(),
                                       action.actor_action().path_action().actor_id());
                break;
            case sseProto::ActorAction::TypeCase::kPositionAction:
                rtAction.reset(new PositionAction(this, mActorId, action));
                AddAction(rtAction);
                break;
            case sseProto::ActorAction::TypeCase::kSpeedAction:
                rtAction.reset(new SpeedAction(this, mActorId, action));
                AddAction(rtAction);
                break;
            case sseProto::ActorAction::TypeCase::kChangeParameterAction:
                rtAction.reset(new ChangeParameterAction(this, mActorId, action));
                AddAction(rtAction);
                break;
            case sseProto::ActorAction::TypeCase::kUserDefinedAction:
                rtAction.reset(new UserDefinedAction(this, mActorId, action));
                AddAction(rtAction);
                break;
            case sseProto::ActorAction::TypeCase::kLongitudinalDistanceAction:
                rtAction.reset(new LongitudinalDistanceAction(this, mActorId, action));
                AddAction(rtAction);
                break;
            default:
                rrThrow("Invalid actor action data model.");
            }
            break;
        default:
            rrThrow("Invalid actor action phase data model. Every action in this phase must be an actor action.");
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void ActorActionPhase::OnRun() {
    // Create and initialize actor if the actor does not exist
    mExecutor->GetSimulation()->InitializeActor(*this);

    // Notify clients about this action
    mExecutor->NotifyActorActionPhaseStarted(this);

    // Perform parent class operations
    ActionPhase::OnRun();
}

////////////////////////////////////////////////////////////////////////////////

void ActorActionPhase::OnEnd() {
    // Notify that this actor action phase has ended
    if (mState == State::eRun) {
        mExecutor->NotifyActorActionPhaseEnded(this);
    }

    // Perform parent class operations
    ActionPhase::OnEnd();
}

////////////////////////////////////////////////////////////////////////////////

rrOpt<std::string> ActorActionPhase::HasActorCreation() const {
    auto actor = mExecutor->GetSimulation()->FindActor(mActorId, false);
    if (actor == nullptr) {
        return mActorId;
    } else {
        return {};
    }
}

////////////////////////////////////////////////////////////////////////////////

void ActorActionPhase::DetectAndResolveConflict(const ActorActionPhase* newPhase) const {
    // Do nothing if new phase is regarding a different actor
    if (mActorId != newPhase->mActorId) {
        return;
    }

    for (auto newAction : newPhase->GetFinalActions()) {
        // Check conflict for actor action
        if (!newAction->GetDataModel().has_actor_action()) {
            continue;
        }
        auto* newActorAction = dynamic_cast<ActorAction*>(newAction.get());
        rrThrowVerify(newActorAction, "Invalid " + newAction->GetDataModel().id() + " in " + newPhase->GetDataModel().id());

        for (auto action : GetFinalActions()) {
            if (!action->GetDataModel().has_actor_action()) {
                continue;
            }
            auto* actorAction = dynamic_cast<ActorAction*>(action.get());
            if (actorAction && actorAction->InConflictWith(*newActorAction)) {
                // Currently we only support LIFO/Preemptive policy
                actorAction->Preempt(mExecutor);
                rrDev("Action with id: " + action->GetDataModel().id() + " has been preempted.");
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

sseProto::Phase ActorActionPhase::GetUnskippedActions() const {
    // No trim by default
    sseProto::Phase proto = mDataModel;

    // Collect actions to trim
    std::unordered_set<std::string> skippedActions;
    for (auto action : GetFinalActions()) {
        if (action->GetEventStatus() == sseProto::ACTION_EVENT_STATUS_SKIPPED) {
            skippedActions.insert(action->GetDataModel().id());
        }
    }

    if (!skippedActions.empty()) {
        // Trim by excluding skipped actions
        proto.mutable_action_phase()->mutable_actions()->Clear();
        int num = mDataModel.action_phase().actions_size();
        for (int i = 0; i < num; ++i) {
            const auto& action = mDataModel.action_phase().actions(i);
            if (skippedActions.find(action.id()) == skippedActions.end()) {
                proto.mutable_action_phase()->mutable_actions()->Add()->CopyFrom(action);
            }
        }
    }

    return proto;
}

////////////////////////////////////////////////////////////////////////////////

void ActorActionPhase::NotifyActionEventDispatched() const {
    DispatchFinalActions();
}

////////////////////////////////////////////////////////////////////////////////
// SystemActionPhase
////////////////////////////////////////////////////////////////////////////////

SystemActionPhase::SystemActionPhase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto)
    : ActionPhase(executor, proto) {
    rrThrowVerify(proto.action_phase().has_system_action_phase(), "Invalid data model for a system action phase.");

    // Create run-time actions
    int num = proto.action_phase().actions_size();
    for (int i = 0; i < num; ++i) {
        const sseProto::Action& action = proto.action_phase().actions(i);
        switch (action.type_case()) {
        case sseProto::Action::TypeCase::kSystemAction:
            switch (action.system_action().type_case()) {
            case sseProto::SystemAction::TypeCase::kScenarioFailure:
                mFinalActions.push_back(std::make_shared<ScenarioFailure>(this, action));
                break;
            case sseProto::SystemAction::TypeCase::kScenarioSuccess:
                mFinalActions.push_back(std::make_shared<ScenarioSuccess>(this, action));
                break;
            case sseProto::SystemAction::TypeCase::kWaitAction:
                mFinalActions.push_back(std::make_shared<WaitAction>(this, action));
                break;
            case sseProto::SystemAction::TypeCase::kSendEventAction:
                mFinalActions.push_back(std::make_shared<EmitAction>(this, action));
                break;
            default:
                rrThrow("Invalid system action data model.");
            }
            break;
        default:
            rrThrow("Invalid system action phase data model. Every action in this phase must be a system action.");
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void SystemActionPhase::OnRun() {
    // Dispatch final actions directly without arbitrating with actors
    DispatchFinalActions();

    // Perform parent class operations
    ActionPhase::OnRun();
}

////////////////////////////////////////////////////////////////////////////////
} // namespace sse
