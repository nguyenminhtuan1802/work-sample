#pragma once

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "RoadRunnerConversions.h"

#include "mathworks/scenario/simulation/scenario.pb.h"

#include <vector>

namespace sse {
class ScenarioExecutor;
class Phase;
class Condition;

////////////////////////////////////////////////////////////////////////////////
// An action object defines an actor objective or a system operation. The Action
// class is a base class that implements some shared properties and interfaces.
//
class Action {
  public:
    enum class ControlDimension {
        eLongitudinal = 0,
        eLateral      = 1,
    };

    Action(Phase* phase, const sseProto::Action& proto);

    virtual ~Action() {}

    // Get data model of this action
    const sseProto::Action& GetDataModel() const {
        return mDataModel;
    }

    // Transit the action event into DISPATCHED state
    void Dispatch() {
        mEventStatus = sseProto::ACTION_EVENT_STATUS_DISPATCHED;
    }

    // Transit the action event into INTERRUPTED or SKIPPED state
    void Preempt(ScenarioExecutor* executor);
    // Transit the action into DONE state
    void Complete(ScenarioExecutor* executor);

    // Get action event dispatch status
    sseProto::ActionEventStatus GetEventStatus() const {
        return mEventStatus;
    }

    // Get run-time action attributes
    void GetActionStatus(sseProto::ActionStatus* rtAction);
    // Enforce any objectives defined by this action to related actors.
    // This is used during actor creation for initializing the new actor.
    virtual void Enforce(
        ScenarioExecutor*                executor,
        sseProto::Actor&                 actor,
        const std::vector<rrSP<Action>>& phaseActions) const;

    // Apply this action to the run-time scenario. Upon apply, related actors of
    // the scenario may be notified with events. The events indicate the change
    // that is expected to occur.
    virtual void Apply(ScenarioExecutor* executor);

    // Check if the change that this action previously requested has happened.
    virtual bool CheckStatus(ScenarioExecutor* executor);

    // Return if the actor who takes on this action can end related work once the
    // objective of this action is met. Some actions such as a continuous speed
    // action does not end, resulting the actors to continuously matching speed
    // as required.
    virtual bool HasRegularEnding() const {
        return true;
    }

    // Append a list with the actors that this action depends on. For example,
    // speed(3mps, faster than: car2) is a speed action that depends on 'car2'
    // for execution.
    virtual void AddDependency(std::vector<std::string>&) const {}

  protected:
    // Notify clients that an action has completed if necessary
    virtual void NotifyActionComplete(ScenarioExecutor*,
                                      sseProto::ActionEventStatus) const {}

  protected:
    // Pointer to the associated phase with this action
    Phase* const mPhase;
    // Data model associated with this run-time action
    const sseProto::Action& mDataModel;
    // Action event dispatch status
    sseProto::ActionEventStatus mEventStatus = sseProto::ACTION_EVENT_STATUS_UNSPECIFIED;
    // Runtime condition
    rrSP<Condition> mCondition;
};

////////////////////////////////////////////////////////////////////////////////
// Class ActorAction implements actor actions. It acts as the parent class for
// particular actions such as: speed, lane, and position actions.
//
class ActorAction : public Action {
  public:
    // Detect conflict with another actor action
    virtual bool                          InConflictWith(const ActorAction& other) const;
    virtual std::vector<ControlDimension> GetControlDimensions() const = 0;

  protected:
    ActorAction(Phase* phase, const std::string& actor, const sseProto::Action& proto)
        : Action(phase, proto)
        , mActorId(actor) {
        rrThrowVerify(proto.has_actor_action(), "Invalid data model for an action action.");
    }

    void        NotifyActionComplete(ScenarioExecutor*           executor,
                                     sseProto::ActionEventStatus status) const override;
    std::string mActorId;
};

////////////////////////////////////////////////////////////////////////////////
// A LaneChangeAction implements a change to a scenario regarding an actors lane
// location.
//
class LaneChangeAction : public ActorAction {
  public:
    LaneChangeAction(Phase* phase, const std::string& actor, const sseProto::Action& proto);
    void AddDependency(std::vector<std::string>& dependencies) const override;

    std::vector<ControlDimension> GetControlDimensions() const override {
        return {ControlDimension::eLateral};
    }
};

////////////////////////////////////////////////////////////////////////////////
// A LateralOffsetAction implements a positional change of an actor in the
// lateral direction.
//
class LateralOffsetAction : public ActorAction {
  public:
    LateralOffsetAction(Phase* phase, const std::string& actor, const sseProto::Action& proto);
    void AddDependency(std::vector<std::string>& dependencies) const override;

    std::vector<ControlDimension> GetControlDimensions() const override {
        return {ControlDimension::eLateral};
    }
};

////////////////////////////////////////////////////////////////////////////////
// A PathAction implements a change to a scenario regarding the path that an
// actor shall follow.
//
class PathAction : public ActorAction {
  public:
    PathAction(Phase* phase, const std::string& actor, const sseProto::Action& proto)
        : ActorAction(phase, actor, proto) {
        rrThrowVerify(proto.actor_action().has_path_action(), "Invalid data model for a path action.");
    }

    std::vector<ControlDimension> GetControlDimensions() const override {
        return {};
    }
};

////////////////////////////////////////////////////////////////////////////////
// A PositionAction implements a change to a scenario regarding the position
// of an actor.
//
class PositionAction : public ActorAction {
  public:
    PositionAction(Phase* phase, const std::string& actor, const sseProto::Action& proto)
        : ActorAction(phase, actor, proto) {
        rrThrowVerify(proto.actor_action().has_position_action(), "Invalid data model for a position action.");
    }

    bool CheckStatus(ScenarioExecutor*) override;

    std::vector<ControlDimension> GetControlDimensions() const override {
        return {};
    }
};

////////////////////////////////////////////////////////////////////////////////
// A SpeedAction implements a change to a scenario regarding the speed of an
// actor.
//
class SpeedAction : public ActorAction {
  public:
    SpeedAction(Phase* phase, const std::string& actor, const sseProto::Action& proto);
    void Enforce(
        ScenarioExecutor*                executor,
        sseProto::Actor&                 actor,
        const std::vector<rrSP<Action>>& phaseActions) const override;
    bool HasRegularEnding() const override;
    void AddDependency(std::vector<std::string>& dependencies) const override;

    std::vector<ControlDimension> GetControlDimensions() const override {
        return {ControlDimension::eLongitudinal};
    }
};

////////////////////////////////////////////////////////////////////////////////
// A LongitudinalDistanceAction implements a speed-control action to maintain a
// longitudinal distance of an ego vehicle from a reference vehicle
class LongitudinalDistanceAction : public ActorAction {
  public:
    LongitudinalDistanceAction(Phase* phase, const std::string& actor, const sseProto::Action& proto);
    bool HasRegularEnding() const override;
    void AddDependency(std::vector<std::string>& dependencies) const override;

    std::vector<ControlDimension> GetControlDimensions() const override {
        return {ControlDimension::eLongitudinal};
    }
};

////////////////////////////////////////////////////////////////////////////////
// A ChangeParameterAction implements a parameter change associated with
// an actor.
//
class ChangeParameterAction : public ActorAction {
  public:
    ChangeParameterAction(Phase* phase, const std::string& actor, const sseProto::Action& proto)
        : ActorAction(phase, actor, proto) {
        rrThrowVerify(proto.actor_action().has_change_parameter_action(),
                      "Invalid data model for a set parameter action.");
    }

    bool InConflictWith(const ActorAction& other) const override;

    std::vector<ControlDimension> GetControlDimensions() const override {
        return {};
    }

    void Apply(ScenarioExecutor* executor) override;
};

////////////////////////////////////////////////////////////////////////////////
// A UserDefinedAction sends a custom action to a specified actor to inform it
// to perform specified operations.
//
class UserDefinedAction : public ActorAction {
  public:
    UserDefinedAction(Phase* phase, const std::string& actor, const sseProto::Action& proto)
        : ActorAction(phase, actor, proto) {
        rrThrowVerify(proto.actor_action().has_user_defined_action(), "Invalid data model for a user-defined action.");
        mInstantaneous = proto.actor_action().user_defined_action().instantaneous();
    }

    void Apply(ScenarioExecutor*) override;

    bool CheckStatus(ScenarioExecutor*) override {
        return mInstantaneous;
    }

    std::vector<ControlDimension> GetControlDimensions() const override {
        return {};
    }

  private:
    bool mInstantaneous = false;
};

////////////////////////////////////////////////////////////////////////////////
// Class SystemAction implements a zero-time system operation in a scenario.
//
// Zero-time operations include:
// - ScenarioSuccess: operation to pass and end a scenario test
// - ScenarioFailure: operation to fail and end a scenario test
//
// Many zero-time operations are conditional, e.g., with syntax such as:
//   on @condition
//       ScenarioFailure()
//
class SystemAction : public Action {
  public:
    SystemAction(Phase* phase, const sseProto::Action& proto)
        : Action(phase, proto) {
        rrThrowVerify(proto.has_system_action(), "Invalid data model for a zero-time phase.");
    }
};

////////////////////////////////////////////////////////////////////////////////
// Class ScenarioSuccess implements a zero-time operation to pass and end the
// scenario test.
//
class ScenarioSuccess : public SystemAction {
  public:
    ScenarioSuccess(Phase* phase, const sseProto::Action& proto)
        : SystemAction(phase, proto) {
        rrThrowVerify(proto.system_action().has_scenario_success(), "Invalid data model for a scenario success operation.");
    }

    void Apply(ScenarioExecutor*) override;
};

////////////////////////////////////////////////////////////////////////////////
// Class ScenarioFailure implements a zero-time operation to fail and end the
// scenario test.
//
class ScenarioFailure : public SystemAction {
  public:
    ScenarioFailure(Phase* phase, const sseProto::Action& proto)
        : SystemAction(phase, proto) {
        rrThrowVerify(proto.system_action().has_scenario_failure(), "Invalid data model for a scenario failure operation.");
    }

    void Apply(ScenarioExecutor*) override;
};

////////////////////////////////////////////////////////////////////////////////
// Class WaitAction implements an action for the system to wait for the end
// condition of the owning phase to become true.
//
class WaitAction : public SystemAction {
  public:
    WaitAction(Phase* phase, const sseProto::Action& proto)
        : SystemAction(phase, proto) {
        rrThrowVerify(proto.system_action().has_wait_action(), "Invalid data model for a wait action.");
    }
};

////////////////////////////////////////////////////////////////////////////////
// Class EmitAction implements an action for the system to emit and notify a
// custom event.
//
class EmitAction : public SystemAction {
  public:
    EmitAction(Phase* phase, const sseProto::Action& proto)
        : SystemAction(phase, proto) {
        rrThrowVerify(proto.system_action().has_send_event_action(),
                      "Invalid data model for a send event action.");
        rrThrowVerify(!proto.system_action().send_event_action().custom_command().name().empty(),
                      "Invalid data model for a send event action. "
                      "A custom event must have a name.");
    }

    void Apply(ScenarioExecutor*) override;
};
} // namespace sse
