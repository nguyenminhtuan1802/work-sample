#pragma once

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "RoadRunnerConversions.h"
#include "Parameter.h"

#include "mathworks/scenario/common/geometry.pb.h"
#include "mathworks/scenario/simulation/actor.pb.h"
#include "mathworks/scenario/simulation/condition.pb.h"
#include "mathworks/scenario/simulation/comparison.pb.h"

#include <set>
#include <unordered_map>
#include <array>
#include <vector>

namespace geometry {
class PolyLine;
}

namespace sse {
class Phase;
class ScenarioExecutor;

////////////////////////////////////////////////////////////////////////////////
// Class Condition is an abstract base class that defines the interfaces of
// run-time condition objects.
//
class Condition {
  public:
    virtual ~Condition() {}

    // Apply the condition by initializing or activating some runtime values
    virtual void Apply(ScenarioExecutor*) {}

    // Check if the condition is satisfied
    virtual bool Check(ScenarioExecutor*) = 0;

    // Update actors related to this condition so that the condition is met.
    // This is required for initializing a new actor with values specified
    // by an 'at start' modifier.
    virtual void Enforce(ScenarioExecutor*, sseProto::Actor&) const {}

    // Append a list with all the actors that this condition depends on. For
    // example, 'CartesianDistance(car2, less than: 10m)' set a dependency to
    // actor 'car2'
    virtual void AddDependency(std::vector<std::string>&) const {}

    // Append a list with all the actors that this condition depends on when it
    // is used as an end condition. When this condition is used as an end
    // condition of an action, the condition will not be evaluated until the
    // action is committed in the next step. As a result, the dependencies are
    // limited to those actors that are referenced upon apply/initialize of this
    // condition.
    virtual void AddDependencyOfApply(std::vector<std::string>&) const {}

    // Get runtime state of this condition
    virtual void GetConditionStatus(sseProto::ConditionStatus* rtCondition);

    // Get a textual summary of the current status of the condition
    virtual std::string GetSummary() const {
        if (mState.has_value()) {
            return (mState.value()) ? "The condition was evaluated, but was not satisfied" : "The condition was evaluated, and was satisfied";
        } else {
            return "The condition has not yet been evaluated";
        }
    }

    bool IsSatisfied() const {
        return mState.has_value() ? mState.value() : false;
    }

    static rrSP<Condition> CreateCondition(const sseProto::Condition& proto);

  protected:
    // State of the condition when it is checked last time
    rrOpt<bool> mState;

    // Id of condition
    std::string mId;

    // Tolerance used by conditions for time value comparison
    static const double cDefaultTolerance;
};

////////////////////////////////////////////////////////////////////////////////
// Class CompositeCondition is the parent class of all the composite conditions,
// such as an OR condition and an AND condition.
//
class CompositeCondition : public Condition {
  public:
    void Apply(ScenarioExecutor*) override;
    void AddDependency(std::vector<std::string>&) const override;
    void AddDependencyOfApply(std::vector<std::string>&) const override;

  protected:
    CompositeCondition(const sseProto::Condition& proto);

    // Operands of the Boolean operations
    std::vector<rrSP<Condition>> mOperands;
};

////////////////////////////////////////////////////////////////////////////////
// Class OrCondition implements Boolean OR operations on a set of conditions,
// such as: cond1 OR cond2 OR ... OR condN.
//
class OrCondition : public CompositeCondition {
  public:
    OrCondition(const sseProto::Condition& proto)
        : CompositeCondition(proto) {
        rrThrowVerify(proto.composite_condition().has_or_condition(),
                      "Invalid data model for an OR condition.");
    }

    bool        Check(ScenarioExecutor*) override;
    void        GetConditionStatus(sseProto::ConditionStatus* rtCondition) override;
    std::string GetSummary() const override;
};

////////////////////////////////////////////////////////////////////////////////
// Class AndCondition implements Boolean AND operations on a set of conditions,
// such as: cond1 AND cond2 AND ... AND condN.
//
class AndCondition : public CompositeCondition {
  public:
    AndCondition(const sseProto::Condition& proto)
        : CompositeCondition(proto) {
        rrThrowVerify(proto.composite_condition().has_and_condition(),
                      "Invalid data model for an AND condition.");
    }

    bool        Check(ScenarioExecutor*) override;
    void        GetConditionStatus(sseProto::ConditionStatus* rtCondition) override;
    std::string GetSummary() const override;
};

////////////////////////////////////////////////////////////////////////////////
// Class LanePosition defines a location on a lane and is equivalent to LanePosition
// protobuf data model
//
class LanePosition {
  public:
    LanePosition() = default;

    LanePosition(const std::string& id, double s, double offset)
        : mLaneId(id)
        , mSValue(s)
        , mOffset(offset) {}

    std::string mLaneId;
    double      mSValue = 0.0;
    double      mOffset = 0.0;
};

////////////////////////////////////////////////////////////////////////////////
// Class VehicleMapLocations contains the run-time lane mapping data of an actor and is
// equivalent to the VehicleMapLocations protobuf data model
//
class VehicleMapLocations {
  public:
    VehicleMapLocations() = default;

    // void AddLanePosition(const LanePosition& lanePosition);
    // bool HasOnLane() const { return mHasOnLane; }

    bool mHasOnLane = false;
    // Index of the best aligned lane position
    int32_t                   mBestLaneIdx = 0;
    double                    mAngle       = 0.0;
    std::vector<LanePosition> mLanePositions;
};

////////////////////////////////////////////////////////////////////////////////
// Class SingularCondition is the parent class of all the singular conditions,
// such as a simulation time condition or a duration condition.
//
class SingularCondition : public Condition {
  protected:
    SingularCondition()
        : Condition() {
    }

    VehicleMapLocations GetActorMapLocations(ScenarioExecutor*  executor,
                                             const std::string& actorId) const;
};

////////////////////////////////////////////////////////////////////////////////
// Class DurationCondition implements a run-time condition that tracks how much
// time has elapse since start of a timer. It report whether the timer exceeds
// a specified value.
//
class DurationCondition : public SingularCondition {
  public:
    DurationCondition(const sseProto::DurationCondition& proto)
        : SingularCondition()
        , mDuration(proto.duration_time()) {
        rrThrowVerify(proto.duration_time() >= 0, "Duration time value must be greater than or equal to 0");
    }

    void        Apply(ScenarioExecutor*) override;
    bool        Check(ScenarioExecutor*) override;
    void        GetConditionStatus(sseProto::ConditionStatus* rtCondition) override;
    std::string GetSummary() const override;

  private:
    double mDuration       = 0.0;
    double mTimerStartTime = 0.0;
    double mElapsedTime    = 0.0;
};

////////////////////////////////////////////////////////////////////////////////
// Class PhaseStateCondition implements a run-time condition that checks if
// a phase has reached a given state
//
class PhaseStateCondition : public SingularCondition {
  public:
    PhaseStateCondition(const sseProto::PhaseStateCondition& proto)
        : SingularCondition() {
        mPhaseId    = proto.phase_id();
        mPhaseState = proto.phase_state();
    }

    void        Apply(ScenarioExecutor*) override;
    bool        Check(ScenarioExecutor*) override;
    void        GetConditionStatus(sseProto::ConditionStatus* rtCondition) override;
    std::string GetSummary() const override;

  private:
    std::string          mPhaseId;
    sseProto::PhaseState mPhaseState;
    rrSP<Phase>          mPhase;
};

////////////////////////////////////////////////////////////////////////////////
// Class SimulationTimeCondition implements a run-time condition that checks if
// the current simulation clock time exceeds a specified value.
//
class SimulationTimeCondition : public SingularCondition {
  public:
    SimulationTimeCondition(double time)
        : SingularCondition()
        , mDueTime(time) {
        rrThrowVerify(time >= 0, "Time value must be greater than or equal to 0");
    }

    bool        Check(ScenarioExecutor*) override;
    void        GetConditionStatus(sseProto::ConditionStatus* rtCondition) override;
    std::string GetSummary() const override;

  private:
    double mDueTime        = 0.0;
    double mSimulationTime = 0.0;
};

////////////////////////////////////////////////////////////////////////////////
// Class DistanceCondition measures the closest Cartesian distance
// between two objects, and compare the distance using a specified rule. Its
// subclasses support different object types, such as: point to point, point to
// actor, and actor to actor.
//
class DistanceCondition : public SingularCondition {
  public:
    DistanceCondition(
        double                       threshold,
        sseProto::DistanceComparison rule)
        : SingularCondition()
        , mThreshold(threshold)
        , mRule(rule) {
        rrThrowVerify(threshold >= 0,
                      "Threshold value for distance comparison must be greater than or equal to zero");
        rrThrowVerify(rule != sseProto::DISTANCE_COMPARISON_UNSPECIFIED,
                      "Invalid distance comparison option");
    }

    void GetConditionStatus(sseProto::ConditionStatus* rtCondition) override;

  protected:
    bool        Compare(double distance, double tolerance = cDoubleEpsilon);
    std::string GetSummary(const std::string& targetType) const;
    using Condition::GetSummary;

    double                       mThreshold = 0.0;
    sseProto::DistanceComparison mRule;

    double                mDistance = 0.0;
    std::array<double, 3> mRuntimeActorPoint{0.0, 0.0, 0.0};
    std::array<double, 3> mRuntimeRefObjPoint{0.0, 0.0, 0.0};
};

////////////////////////////////////////////////////////////////////////////////
// Cartesian distance between an actor and a 3D point
//
class ActorToPointCartesianDistanceCondition : public DistanceCondition {
  public:
    ActorToPointCartesianDistanceCondition(
        const std::string&             actorId,
        double                         threshold,
        sseProto::DistanceComparison   rule,
        const sseProtoCommon::Vector3& point);

    void AddDependency(std::vector<std::string>& dependencies) const override {
        dependencies.push_back(mActorId);
    }

    bool        Check(ScenarioExecutor*) override;
    std::string GetSummary() const override;

  private:
    std::string           mActorId;
    std::array<double, 3> mPoint;
};

////////////////////////////////////////////////////////////////////////////////
// Cartesian distance between two actors
//
class ActorToActorCartesianDistanceCondition : public DistanceCondition {
  public:
    ActorToActorCartesianDistanceCondition(
        const std::string&           actorId,
        double                       threshold,
        sseProto::DistanceComparison rule,
        const std::string&           refActorId);
    bool Check(ScenarioExecutor*) override;

    void AddDependency(std::vector<std::string>& dependencies) const override {
        dependencies.push_back(mActorId);
        dependencies.push_back(mRefActorId);
    }

    std::string GetSummary() const override;

  private:
    std::string mActorId;
    std::string mRefActorId;
};

////////////////////////////////////////////////////////////////////////////////
// Longitudinal distance between two actors
//
class LongitudinalDistanceCondition : public DistanceCondition {
  public:
    LongitudinalDistanceCondition(
        const std::string&              actorId,
        const sseProto::DistanceTarget& distanceTarget);
    void        Apply(ScenarioExecutor*) override;
    bool        Check(ScenarioExecutor*) override;
    void        AddDependency(std::vector<std::string>&) const override;
    std::string GetSummary() const override;

  private:
    std::string                         mActorId;
    std::string                         mRefActorId;
    sseProto::MeasureFrom               mMeasureFrom;
    sseProto::ActorCoordinateSystemType mCoordinateSystem;
    sseProto::DistanceType              mDistanceType;
    sseProto::PositionComparison        mPositionComparison;
    double                              mTimeDistance;
};

////////////////////////////////////////////////////////////////////////////////
// Class ActorSpeedCondition implements a run-time condition that checks if
// current speed of an actor meets a specified numeric comparison condition.
//
class ActorSpeedCondition : public SingularCondition {
  public:
    ActorSpeedCondition(
        const std::string&           actorId,
        const sseProto::SpeedTarget& speedTarget);
    void        Apply(ScenarioExecutor*) override;
    bool        Check(ScenarioExecutor*) override;
    void        Enforce(ScenarioExecutor*, sseProto::Actor&) const override;
    void        AddDependency(std::vector<std::string>&) const override;
    void        AddDependencyOfApply(std::vector<std::string>&) const override;
    void        GetConditionStatus(sseProto::ConditionStatus* rtCondition) override;
    std::string GetSummary() const override;

  private:
    double GetActorSpeed(ScenarioExecutor*, const std::string& actorId) const;

    bool IsRelative(sseProto::SpeedComparison type) const {
        return (type == sseProto::SPEED_COMPARISON_SAME_AS ||
                type == sseProto::SPEED_COMPARISON_FASTER_THAN ||
                type == sseProto::SPEED_COMPARISON_SLOWER_THAN);
    }

    std::string                     mActorId;
    double                          mMin = 0.0;
    double                          mMax = 0.0;
    const sseProto::SpeedReference& mSpeedReference;
    const sseProto::ComparisonRule  mComparisonRule;
    double                          mSpeed    = 0.0;
    double                          mRefSpeed = 0.0;
};

////////////////////////////////////////////////////////////////////////////////

enum class rrSide {
    eLeft = 0,
    eRight
};

////////////////////////////////////////////////////////////////////////////////
// Class ActorLaneCondition implements a run-time condition that checks for
// completion of a lane change action
//
class ActorLaneCondition : public SingularCondition {
  public:
    ActorLaneCondition(
        const std::string&             actorId,
        int                            min,
        int                            max,
        const sseProto::LaneReference& rule);
    void Apply(ScenarioExecutor*) override;
    bool Check(ScenarioExecutor*) override;
    void AddDependency(std::vector<std::string>&) const override;
    void GetConditionStatus(sseProto::ConditionStatus* rtCondition) override;

  private:
    std::string                    mActorId;
    int                            mMin = 0;
    int                            mMax = 0;
    rrSide                         mSide;
    const sseProto::LaneReference& mRule;
    VehicleMapLocations            mTargetLocations;
    bool                           mLoopbackConditionDetected = false;
};

////////////////////////////////////////////////////////////////////////////////
// Class ActorLateralOffsetCondition implements a run-time condition that checks for
// completion of a lateral offset action
//
class ActorLateralOffsetCondition : public SingularCondition {
  public:
    ActorLateralOffsetCondition(
        const std::string& actorId,
        double             offsetValue);
    void Apply(ScenarioExecutor*) override;
    bool Check(ScenarioExecutor*) override;
    void AddDependency(std::vector<std::string>&) const override;
    void GetConditionStatus(sseProto::ConditionStatus* rtCondition) override;

  private:
    std::string                mActorId;
    double                     mOffsetValue;
    rrOpt<geometry::PolyLine>  mPath;
    rrOpt<VehicleMapLocations> mTargetMapLocation;
    bool                       mLoopbackConditionDetected = false;
};

////////////////////////////////////////////////////////////////////////////////
// Class CollisionCondition implements a run-time condition that checks if
// actors have collided with each other.
//
class CollisionCondition : public SingularCondition {
  public:
    CollisionCondition(const sseProto::CollisionCondition& proto);
    // Check if collision occurs. Computation is based on bounding box overlap.
    // For higher fidelity collision check, actor shall compute from its own
    // behavior model.
    bool Check(ScenarioExecutor*) override;

    // Report dependencies that this condition has on actors.
    void AddDependency(std::vector<std::string>& dependencies) const override {
        std::set<std::string> actorIds = mActorIds;
        actorIds.insert(mRefActorIds.begin(), mRefActorIds.end());
        for (const auto& id : actorIds) {
            dependencies.push_back(id);
        }
    }

    void        GetConditionStatus(sseProto::ConditionStatus* rtCondition) override;
    std::string GetSummary() const override;

  private:
    // Using set rather than unordered_set for consistent results across
    // platforms/executions in certain edge cases
    std::set<std::string> mActorIds;
    std::set<std::string> mRefActorIds;

    struct ActorHash {
        std::size_t operator()(std::pair<const sseProto::Actor*,
                                         const sseProto::Actor*> const& pair) const {
            return std::hash<std::string>()(pair.first->actor_spec().id()) ^
                   std::hash<std::string>()(pair.second->actor_spec().id());
        }
    };

    std::unordered_set<std::pair<const sseProto::Actor*, const sseProto::Actor*>, ActorHash> mCollisions;
};

////////////////////////////////////////////////////////////////////////////////
// Class ParameterComparisonCondition is a base class for all parameter conditions
//
class ParameterComparisonCondition : public SingularCondition {
  public:
    struct ParameterComparison {
        std::string              mComparedValue;
        sseProto::ComparisonRule mRule = sseProto::COMPARISON_RULE_EQUAL_TO;
    };

    static rrOpt<roadrunner::proto::Parameter> FindParameter(
        const google::protobuf::RepeatedPtrField<sseProto::Attribute>& parameterCollection,
        const std::string&                                             parameterName);

  protected:
    bool Compare(const std::unordered_map<std::string, roadrunner::proto::Parameter>& parameterNameValueMap, double tolerance = cDoubleEpsilon);
    bool CompareString(const std::string& val, const std::string& comparedVal, sseProto::ComparisonRule rule, double tolerance = cDoubleEpsilon);
    // *IMPORTANT* special handling for Simulink/MATLAB agent because their parameters are always string
    // - Other parameters must use Compare
    bool CompareBehaviorParameterForSimulinkAgent(
        const std::unordered_map<std::string, roadrunner::proto::Parameter>& parameterNameValueMap,
        double                                                               tolerance = cDoubleEpsilon);

  protected:
    std::unordered_map<std::string, ParameterComparison> mParameterExpressionsMap;
};

////////////////////////////////////////////////////////////////////////////////
// Class CustomEventCondition implements a run-time condition that checks if a
// custom event with a specified name has occurred since last time the scenario
// is updated.
//
class CustomEventCondition : public ParameterComparisonCondition {
  public:
    CustomEventCondition(const sseProto::EventCondition& proto);
    void Apply(ScenarioExecutor*) override;
    bool Check(ScenarioExecutor*) override;

  private:
    std::string mEventName;
};

////////////////////////////////////////////////////////////////////////////////
// Class ParameterCondition implements a run-time condition that compares a behavior
// or actor parameter against a specified value
//
class ParameterCondition : public ParameterComparisonCondition {
  public:
    ParameterCondition(const sseProto::ParameterCondition& proto);

    bool Check(ScenarioExecutor* executor) override;

  private:
    std::string             mActorId;
    sseProto::ParameterType mParameterType = sseProto::PARAMETER_TYPE_ACTOR;
};

////////////////////////////////////////////////////////////////////////////////
// Class ActionsCompleteCondition implements a run-time condition that checks if
// all the actions of a phase have completed.
// - For an action phase, these include the set of actions that are directly
//   contained in the phase
// - For a composite phase (such as a parallel phase), the set of actions include
//   all the child actions in the nested phase hierarchy
//
class ActionsCompleteCondition : public SingularCondition {
  public:
    ActionsCompleteCondition(const sseProto::ActionsCompleteCondition& proto)
        : SingularCondition() {
        mPhaseId = proto.phase_id();
    }

    void        Apply(ScenarioExecutor*) override;
    bool        Check(ScenarioExecutor*) override;
    std::string GetSummary() const override;

  private:
    std::string mPhaseId;
    rrSP<Phase> mPhase;
};

////////////////////////////////////////////////////////////////////////////////
// Longitudinal distance between two actors
//
class TimeToActorCondition : public DistanceCondition {
  public:
    TimeToActorCondition(
        const std::string&              actorId,
        const sseProto::DistanceTarget& distanceTarget);
    void        Apply(ScenarioExecutor*) override;
    bool        Check(ScenarioExecutor*) override;
    void        AddDependency(std::vector<std::string>&) const override;
    std::string GetSummary() const override;

  private:
    std::string                         mActorId;
    std::string                         mRefActorId;
    sseProto::MeasureFrom               mMeasureFrom;
    sseProto::ActorCoordinateSystemType mCoordinateSystem;
    double                              mTime;
};

} // namespace sse
