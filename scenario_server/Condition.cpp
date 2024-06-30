#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "mw_grpc_clients/scenario_server/ScenarioExecutor.hpp"

#include "Condition.h"

#include "EnumConversion.h"
#include "EventCalendar.h"
#include "PhaseDiagramExecutor.h"
#include "RoadRunnerConversions.h"
#include "ScenarioConfiguration.h"
#include "Simulation.h"
#include "Utility.h"

#include <iostream>

namespace sse {
// Tolerance for time value comparison
const double Condition::cDefaultTolerance = 0.001;

////////////////////////////////////////////////////////////////////////////////
// Condition
////////////////////////////////////////////////////////////////////////////////

rrSP<Condition> Condition::CreateCondition(
    const sseProto::Condition& proto) {
    rrSP<Condition> condition;
    switch (proto.type_case()) {
    case sseProto::Condition::kCompositeCondition:
        switch (proto.composite_condition().type_case()) {
        case sseProto::CompositeCondition::kOrCondition:
            condition.reset(new OrCondition(proto));
            break;
        case sseProto::CompositeCondition::kAndCondition:
            condition.reset(new AndCondition(proto));
            break;
        default:
            rrThrow("Unsupported composite condition type.");
        }
        break;
    case sseProto::Condition::kSingularCondition:
        switch (proto.singular_condition().type_case()) {
        case sseProto::SingularCondition::kActorSpeedCondition: {
            const sseProto::ActorSpeedCondition& speedCond =
                proto.singular_condition().actor_speed_condition();
            condition.reset(new ActorSpeedCondition(
                speedCond.actor_id(), speedCond.speed_target()));
            break;
        }
        case sseProto::SingularCondition::kDistanceCondition: {
            const sseProto::DistanceCondition& distCond =
                proto.singular_condition().distance_condition();
            rrThrowVerify(distCond.has_distance_target() && distCond.distance_target().has_distance_reference(), "Invalid data model for " + proto.id());

            switch (distCond.distance_target().distance_dimension_type()) {
            case sseProto::DISTANCE_DIMENSION_TYPE_EUCLIDEAN: {
                switch (distCond.distance_target().distance_reference().reference_type_case()) {
                case sseProto::DistanceReference::kPoint: {
                    condition.reset(new ActorToPointCartesianDistanceCondition(
                        distCond.actor_id(),
                        distCond.distance_target().value(),
                        distCond.distance_target().distance_reference().rule(),
                        distCond.distance_target().distance_reference().point()));
                    break;
                }
                case sseProto::DistanceReference::kReferenceActorId: {
                    condition.reset(new ActorToActorCartesianDistanceCondition(
                        distCond.actor_id(),
                        distCond.distance_target().value(),
                        distCond.distance_target().distance_reference().rule(),
                        distCond.distance_target().distance_reference().reference_actor_id()));
                    break;
                }
                default:
                    rrThrow("Unsupported reference object in a Distance condition.");
                }
                break;
            }
            case sseProto::DISTANCE_DIMENSION_TYPE_LONGITUDINAL: {
                condition.reset(new LongitudinalDistanceCondition(
                    distCond.actor_id(),
                    distCond.distance_target()));
                break;
            }
            default:
                break;
            }
            break;
        }
        case sseProto::SingularCondition::kCollisionCondition:
            condition.reset(new CollisionCondition(proto.singular_condition().collision_condition()));
            break;
        case sseProto::SingularCondition::kDurationCondition:
            condition.reset(new DurationCondition(proto.singular_condition().duration_condition()));
            break;
        case sseProto::SingularCondition::kPhaseStateCondition:
            condition.reset(new PhaseStateCondition(proto.singular_condition().phase_state_condition()));
            break;
        case sseProto::SingularCondition::kSimulationTimeCondition: {
            double time = proto.singular_condition().simulation_time_condition().time();
            condition.reset(new SimulationTimeCondition(time));
            break;
        }
        case sseProto::SingularCondition::kParameterCondition:
            condition.reset(new ParameterCondition(proto.singular_condition().parameter_condition()));
            break;
        case sseProto::SingularCondition::kActionsCompleteCondition:
            condition.reset(new ActionsCompleteCondition(proto.singular_condition().actions_complete_condition()));
            break;
        /*case sseProto::SingularCondition::kTimeToActorCondition:
        {
                auto timeToCollisionCond = proto.singular_condition().time_to_actor_condition();
                rrThrowVerify(timeToCollisionCond.has_distance_target() && timeToCollisionCond.distance_target().has_distance_reference()
                        , "Invalid data model for %1"_Q % ConditionUrl(proto.id()));
                condition.reset(new TimeToActorCondition(
                        timeToCollisionCond.actor_id(), timeToCollisionCond.distance_target()));
                break;
        }*/
        case sseProto::SingularCondition::kEventCondition:
            condition.reset(new CustomEventCondition(proto.singular_condition().event_condition()));
            break;
        default:
            rrThrow("Unsupported singular condition type.");
        }
        break;
    default:
        rrThrow("Unsupported condition type.");
    }
    condition->mId = proto.id();
    return condition;
}

////////////////////////////////////////////////////////////////////////////////

void Condition::GetConditionStatus(sseProto::ConditionStatus* rtCondition) {
    rtCondition->set_id(mId);
    sseProto::ConditionState state = mState.has_value() ? (mState.value() ? sseProto::ConditionState::CONDITION_STATE_SATISFIED : sseProto::ConditionState::CONDITION_STATE_UNSATISFIED) : (sseProto::ConditionState::CONDITION_STATE_NOT_YET_EVALUATED);
    rtCondition->set_condition_state(state);
}

////////////////////////////////////////////////////////////////////////////////
// CompositeCondition
////////////////////////////////////////////////////////////////////////////////

CompositeCondition::CompositeCondition(const sseProto::Condition& proto) {
    rrThrowVerify(proto.has_composite_condition(),
                  "Invalid data model for a composite condition.");

    // Create children
    int num = proto.composite_condition().operands_size();
    rrThrowVerify(num > 0, "A composite condition must contain 1 or more conditions.");
    for (int i = 0; i < num; ++i) {
        const sseProto::Condition& operandProto = proto.composite_condition().operands(i);
        rrSP<Condition>            operand      = CreateCondition(operandProto);
        mOperands.push_back(operand);
    }
}

////////////////////////////////////////////////////////////////////////////////

void CompositeCondition::Apply(ScenarioExecutor* executor) {
    for (const auto& operand : mOperands) {
        operand->Apply(executor);
    }
}

////////////////////////////////////////////////////////////////////////////////

void CompositeCondition::AddDependency(std::vector<std::string>& dependencies) const {
    for (const auto& operand : mOperands) {
        operand->AddDependency(dependencies);
    }
}

////////////////////////////////////////////////////////////////////////////////

void CompositeCondition::AddDependencyOfApply(std::vector<std::string>& dependencies) const {
    for (const auto& operand : mOperands) {
        operand->AddDependencyOfApply(dependencies);
    }
}

////////////////////////////////////////////////////////////////////////////////
// OrCondition
////////////////////////////////////////////////////////////////////////////////

bool OrCondition::Check(ScenarioExecutor* executor) {
    for (auto& operand : mOperands) {
        mState = operand->Check(executor);
        if (mState.value()) {
            return true;
        }
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////

void OrCondition::GetConditionStatus(sseProto::ConditionStatus* rtCondition) {
    CompositeCondition::GetConditionStatus(rtCondition);
    auto* orStatus = rtCondition->mutable_or_condition_status();
    for (auto& operand : mOperands) {
        operand->GetConditionStatus(orStatus->add_operands_status());
    }
}

////////////////////////////////////////////////////////////////////////////////

std::string OrCondition::GetSummary() const {
    if (mState.has_value()) {
        std::string str1;
        if (mState.value()) {
            for (const rrSP<Condition>& operand : mOperands) {
                if (operand->IsSatisfied()) {
                    return operand->GetSummary();
                }
            }

            rrThrow("Or condition satisfied, but none of its operands were satified.");
        } else {
            return "";
        }
    } else {
        return ("The id: " + mId + " has not been evaluated.");
    }
}

////////////////////////////////////////////////////////////////////////////////
// AndCondition
////////////////////////////////////////////////////////////////////////////////

bool AndCondition::Check(ScenarioExecutor* executor) {
    for (auto& operand : mOperands) {
        mState = operand->Check(executor);
        if (!mState.value()) {
            return false;
        }
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////

void AndCondition::GetConditionStatus(sseProto::ConditionStatus* rtCondition) {
    CompositeCondition::GetConditionStatus(rtCondition);
    auto* andStatus = rtCondition->mutable_and_condition_status();
    for (auto& operand : mOperands) {
        operand->GetConditionStatus(andStatus->add_operands_status());
    }
}

////////////////////////////////////////////////////////////////////////////////

std::string AndCondition::GetSummary() const {
    if (mState.has_value()) {
        std::string str1;
        if (mState.value()) {
            bool andLogicCheck = true;
            for (const rrSP<Condition>& operand : mOperands) {
                if (!operand->IsSatisfied()) {
                    andLogicCheck = false;
                }
            }

            rrThrowVerify(andLogicCheck, "And condition satisfied, but one of its operands was not satified.");
            return "And condition was satisfied.";
        } else {
            return "";
        }
    } else {
        return ("The " + mId + " has not been evaluated.");
    }
}

////////////////////////////////////////////////////////////////////////////////
// PhaseStateCondition
////////////////////////////////////////////////////////////////////////////////

void PhaseStateCondition::Apply(ScenarioExecutor* executor) {
    mPhase = dynamic_cast<PhaseDiagramExecutor*>(executor)->FindPhase(mPhaseId);
}

////////////////////////////////////////////////////////////////////////////////

bool PhaseStateCondition::Check(ScenarioExecutor*) {
    mState = (mPhaseState == sseConvert(mPhase->GetPhaseState()));
    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////

void PhaseStateCondition::GetConditionStatus(sseProto::ConditionStatus* rtCondition) {
    SingularCondition::GetConditionStatus(rtCondition);
    rtCondition->mutable_phase_state_condition_status();
}

////////////////////////////////////////////////////////////////////////////////

std::string PhaseStateCondition::GetSummary() const {
    if (mState.has_value()) {
        if (mState.value()) {
            return ("The " + mPhaseId + " has reached the specified state.");
        } else {
            return ("The " + mPhaseId + " has not reached the specified state.");
        }
    } else {
        return ("The " + mPhaseId + " has not been evaluated.");
    }
}

////////////////////////////////////////////////////////////////////////////////

VehicleMapLocations SingularCondition::GetActorMapLocations(ScenarioExecutor*  executor,
                                                            const std::string& actorId) const {
    // rrSP<Actor> actor = executor->GetSimulation()->FindActor(actorId);
    VehicleMapLocations defaultLoc;
    // return roadrunner::hdmap::MapUtil::GetMapLocations(*actor);
    return defaultLoc;
}

////////////////////////////////////////////////////////////////////////////////
// DurationCondition
////////////////////////////////////////////////////////////////////////////////

void DurationCondition::Apply(ScenarioExecutor* executor) {
    mTimerStartTime = executor->GetSimulation()->GetTimeNow();
}

////////////////////////////////////////////////////////////////////////////////

bool DurationCondition::Check(ScenarioExecutor* executor) {
    auto currentTime = executor->GetSimulation()->GetTimeNow();
    mElapsedTime     = currentTime - mTimerStartTime;
    mState           = (mElapsedTime >= (mDuration - cDefaultTolerance));
    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////

void DurationCondition::GetConditionStatus(sseProto::ConditionStatus* rtCondition) {
    SingularCondition::GetConditionStatus(rtCondition);
    rtCondition->mutable_duration_conditon_status()->set_elapsed_time(mElapsedTime);
}

////////////////////////////////////////////////////////////////////////////////

std::string DurationCondition::GetSummary() const {
    using namespace std;
    if (mState.has_value()) {
        if (mState.value()) {
            return "Duration condition was met.";
        } else {
            return ("Elapsed time: " + to_string(mElapsedTime) + "is less than the duration condition time: " + to_string(mDuration));
        }
    } else {
        return "Duration condition has not been evaluated.";
    }
}

////////////////////////////////////////////////////////////////////////////////
// SimulationTimeCondition
////////////////////////////////////////////////////////////////////////////////

bool SimulationTimeCondition::Check(ScenarioExecutor* executor) {
    mSimulationTime = executor->GetSimulation()->GetTimeNow();
    mState          = (mSimulationTime >= (mDueTime - cDefaultTolerance));
    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////

void SimulationTimeCondition::GetConditionStatus(sseProto::ConditionStatus* rtCondition) {
    SingularCondition::GetConditionStatus(rtCondition);
    rtCondition->mutable_simulation_time_condition_status()->set_simulation_time(mSimulationTime);
}

////////////////////////////////////////////////////////////////////////////////

std::string SimulationTimeCondition::GetSummary() const {
    using namespace std;
    if (mState.has_value()) {
        if (mState.value()) {
            return std::string("Simulation time condition was met.");
        } else {
            return ("Simulation time: " + to_string(mSimulationTime) +
                    " has not exceeded the simulation due time: " + to_string(mDueTime));
        }
    } else {
        return "Simulation time condition was not evaluated.";
    }
}

////////////////////////////////////////////////////////////////////////////////
// DistanceCondition
////////////////////////////////////////////////////////////////////////////////

bool DistanceCondition::Compare(double distance, double tolerance) {
    // Apply distance comparison and rule
    switch (mRule) {
    case sseProto::DistanceComparison::DISTANCE_COMPARISON_GREATER_THAN:
        mState = (distance >= mThreshold - tolerance);
        break;
    case sseProto::DistanceComparison::DISTANCE_COMPARISON_LESS_THAN:
        mState = (distance <= mThreshold + tolerance);
        break;
    case sseProto::DistanceComparison::DISTANCE_COMPARISON_EQUAL_TO:
        mState = EpsilonEqual(distance, mThreshold, tolerance);
        break;
    default:
        rrThrow("Invalid distance comparison option");
    }
    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////

void DistanceCondition::GetConditionStatus(sseProto::ConditionStatus* rtCondition) {
    SingularCondition::GetConditionStatus(rtCondition);
    auto* cartesianDistanceCondition = rtCondition->mutable_distance_condition_status();
    cartesianDistanceCondition->set_distance(mDistance);
    cartesianDistanceCondition->mutable_actor_point()->CopyFrom(sseConvert(mRuntimeActorPoint));
    cartesianDistanceCondition->mutable_reference_object_point()->CopyFrom(sseConvert(mRuntimeRefObjPoint));
}

////////////////////////////////////////////////////////////////////////////////

std::string DistanceCondition::GetSummary(const std::string& targetType) const {
    if (mState.has_value()) {
        return ("Distance to " + targetType + " condition was met.");
    } else {
        return "Cartesian distance rule was not evaluated.";
    }
}

////////////////////////////////////////////////////////////////////////////////
// ActorToPointCartesianDistanceCondition
////////////////////////////////////////////////////////////////////////////////

ActorToPointCartesianDistanceCondition::ActorToPointCartesianDistanceCondition(
    const std::string&             actorId,
    double                         threshold,
    sseProto::DistanceComparison   rule,
    const sseProtoCommon::Vector3& point)
    : DistanceCondition(threshold, rule)
    , mActorId(actorId) {
    // Cache static point
    mPoint[0] = point.x();
    mPoint[1] = point.y();
    mPoint[2] = point.z();
}

////////////////////////////////////////////////////////////////////////////////

bool ActorToPointCartesianDistanceCondition::Check(ScenarioExecutor* executor) {
    // Find actor and report error if not found
    rrSP<sseProto::Actor> actor = executor->GetSimulation()->FindActor(mActorId);

    // Compute oriented bounding box based on current pose of the actor
    geometry::OrientedBox3 box = geometry::GetOrientedBoundingBox(*actor);

    // Compute minimal distance between the oriented box and the specified
    // point. If the point is inside the box, the distance is zero.
    double distance = 0.0;
    if (!InContainer(mPoint, box)) {
        auto result           = DCPQueryAny(mPoint, box);
        distance              = result.distance;
        mRuntimeActorPoint[0] = result.closestPoint.getX();
        mRuntimeActorPoint[1] = result.closestPoint.getY();
        mRuntimeActorPoint[2] = result.closestPoint.getZ();
        mRuntimeRefObjPoint   = mPoint;
    }
    mDistance = distance;

    // Compare
    return Compare(distance);
}

////////////////////////////////////////////////////////////////////////////////

std::string ActorToPointCartesianDistanceCondition::GetSummary() const {
    return DistanceCondition::GetSummary("Point");
}

////////////////////////////////////////////////////////////////////////////////
// ActorToActorCartesianDistanceCondition
////////////////////////////////////////////////////////////////////////////////

ActorToActorCartesianDistanceCondition::ActorToActorCartesianDistanceCondition(
    const std::string&           actorId,
    double                       threshold,
    sseProto::DistanceComparison rule,
    const std::string&           refActorId)
    : DistanceCondition(threshold, rule)
    , mActorId(actorId)
    , mRefActorId(refActorId) {
}

////////////////////////////////////////////////////////////////////////////////

bool ActorToActorCartesianDistanceCondition::Check(ScenarioExecutor* executor) {
    // Find actors and report error if not found
    rrSP<sseProto::Actor> actor    = executor->GetSimulation()->FindActor(mActorId);
    rrSP<sseProto::Actor> refActor = executor->GetSimulation()->FindActor(mRefActorId);

    // Compute oriented bounding box based on current pose of the actors
    geometry::OrientedBox3<double> boxActor    = geometry::GetOrientedBoundingBox(*actor);
    geometry::OrientedBox3<double> boxRefActor = geometry::GetOrientedBoundingBox(*refActor);

    // Compute minimal distance between the two oriented boxes. If collided,
    // the distance is zero.
    double distance = 0.0;
    if (!geometry::TIQuery(boxActor, boxRefActor)) {

        auto findDistance = [&]() {
            auto approxResult   = geometry::ParallelConfigurations::FindDistance(boxActor, boxRefActor);
            distance            = approxResult.mDistance;
            mRuntimeActorPoint  = approxResult.mPoints[0];
            mRuntimeRefObjPoint = approxResult.mPoints[1];
        };

        // Check if two oriented boxes are in parallel configurations
        if (geometry::ParallelConfigurations::CheckParallel(boxActor, boxRefActor)) {
            // Approximate the distance using closest vertices to prevent
            // floating-point error from GTE LCP solver
            findDistance();
        } else {
            // Oriented boxes are not in parallel configurations. Proceed
            // with normal GTE distance query

            /*auto result = DCPQueryAny(boxActor, boxRefActor);
                            if (!result.queryIsSuccessful)*/
            {
                // If LCP solver fails, fall back to approximating distance using closest vertices
                findDistance();
            }
            /*else
            {
                    distance = result.distance;
                    mRuntimeActorPoint = result.closestPoint[0];
                    mRuntimeRefObjPoint = result.closestPoint[1];
            }*/
        }
    }
    mDistance = distance;

    // Compare
    return Compare(distance);
}

////////////////////////////////////////////////////////////////////////////////

std::string ActorToActorCartesianDistanceCondition::GetSummary() const {
    return DistanceCondition::GetSummary("Actor");
}

////////////////////////////////////////////////////////////////////////////////
// LongitudinalDistanceCondition
////////////////////////////////////////////////////////////////////////////////

LongitudinalDistanceCondition::LongitudinalDistanceCondition(
    const std::string&              actorId,
    const sseProto::DistanceTarget& target)
    : DistanceCondition(target.value(), target.distance_reference().rule())
    , mActorId(actorId)
    , mRefActorId(target.distance_reference().reference_actor_id())
    , mMeasureFrom(target.distance_reference().measure_from())
    , mCoordinateSystem(target.distance_reference().actor_coordinate_system_type())
    , mDistanceType(target.distance_type())
    , mPositionComparison(target.distance_reference().position_comparison()) {
    rrThrowVerify(target.distance_dimension_type() == sseProto::DistanceDimensionType::DISTANCE_DIMENSION_TYPE_LONGITUDINAL,
                  "Invalid distance dimension type for longitudinal distance condition. Distance dimension type must be longitudinal.");
    rrThrowVerify(mMeasureFrom != sseProto::MeasureFrom::MEASURE_FROM_UNSPECIFIED,
                  "Invalid measure from type for longitudinal distance condition");
    rrThrowVerify(mCoordinateSystem != sseProto::ActorCoordinateSystemType::ACTOR_COORDINATE_SYSTEM_TYPE_UNSPECIFIED,
                  "Invalid actor coordinate system type for longitudinal distance condition");
    rrThrowVerify(mDistanceType != sseProto::DistanceType::DISTANCE_TYPE_UNSPECIFIED,
                  "Invalid distance type for longitudinal distance condition");
    rrThrowVerify(mPositionComparison != sseProto::PositionComparison::POSITION_COMPARISON_UNSPECIFIED && mPositionComparison != sseProto::PositionComparison::POSITION_COMPARISON_SAME_AS,
                  "Invalid position comparison for longitudinal distance condition");
}

////////////////////////////////////////////////////////////////////////////////

void LongitudinalDistanceCondition::Apply(ScenarioExecutor* /*executor*/) {
    switch (mDistanceType) {
    case sseProto::DistanceType::DISTANCE_TYPE_SPACE:
        // Threshold and runtime distance are of same type, NO OP
        break;
    case sseProto::DistanceType::DISTANCE_TYPE_TIME:
        // Time distance can vary with reference vehicle's speed, hence cache threshold for runtime update
        mTimeDistance = mThreshold;
        break;
    default:
        break;
    }
}

////////////////////////////////////////////////////////////////////////////////

bool LongitudinalDistanceCondition::Check(ScenarioExecutor* executor) {
    rrSP<sseProto::Actor> actor    = executor->GetSimulation()->FindActor(mActorId);
    rrSP<sseProto::Actor> refActor = executor->GetSimulation()->FindActor(mRefActorId);

    /*const auto& result = executor->GetSimulation()->GetMapService()
            .ComputeLongitudinalDistance(*actor, *refActor, mMeasureFrom, mCoordinateSystem);

    if (result.has_value())
    {
            mDistance = result->mValue;
            mRuntimeActorPoint = result->mFirstPoint;
            mRuntimeRefObjPoint = result->mSecondPoint;
    }
    else*/
    {
        mState = false;
        return mState.value();
    }

    switch (mDistanceType) {
    case sseProto::DistanceType::DISTANCE_TYPE_SPACE:
        // Threshold and runtime distance are of same type, NO OP
        break;
    case sseProto::DistanceType::DISTANCE_TYPE_TIME:
        // Compute runtime threshold value
        mThreshold = mTimeDistance * Util::GetSpeed(*executor->GetSimulation()->FindActor(mActorId));
        break;
    default:
        break;
    }

    switch (mPositionComparison) {
    case sseProto::PositionComparison::POSITION_COMPARISON_BEHIND:
        if (mDistance > 0) {
            Compare(mDistance, ScenarioConfiguration::GetLongitudinalOffsetComparisonTolerance());
        } else {
            mState = false;
        }
        break;
    case sseProto::PositionComparison::POSITION_COMPARISON_AHEAD_OF:
        if (mDistance < 0) {
            Compare(-mDistance, ScenarioConfiguration::GetLongitudinalOffsetComparisonTolerance());
        } else {
            mState = false;
        }
        break;
    case sseProto::PositionComparison::POSITION_COMPARISON_EITHER:
        Compare(std::abs(mDistance), ScenarioConfiguration::GetLongitudinalOffsetComparisonTolerance());
        break;
    default:
        break;
    }

    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////

void LongitudinalDistanceCondition::AddDependency(std::vector<std::string>& dependencies) const {
    dependencies.push_back(mActorId);
    dependencies.push_back(mRefActorId);
}

////////////////////////////////////////////////////////////////////////////////

std::string LongitudinalDistanceCondition::GetSummary() const {
    if (mState.has_value()) {
        return ("Longitudinal distance condition was met.");
    } else {
        return "Longitudinal distance condition was not evaluated.";
    }
}

////////////////////////////////////////////////////////////////////////////////
// CollisionCondition
////////////////////////////////////////////////////////////////////////////////

CollisionCondition::CollisionCondition(const sseProto::CollisionCondition& proto)
    : SingularCondition() {
    // Initialize mActorIds (identifiers of actors in group 1)
    int num1 = proto.actor_ids_size();
    for (int i = 0; i < num1; ++i) {
        mActorIds.insert(proto.actor_ids(i));
    }

    // Initialize mRefActorIds (identifiers of actors in group 2)
    int num2 = proto.reference_actor_ids_size();
    for (int i = 0; i < num2; ++i) {
        mRefActorIds.insert(proto.reference_actor_ids(i));
    }
}

////////////////////////////////////////////////////////////////////////////////

bool CollisionCondition::Check(ScenarioExecutor* executor) {
    // Check and record any collision between two actors
    auto checkCollision = [&](const sseProto::Actor& a, const sseProto::Actor& b) {
        // Skip collision check if two actors are parent and child
        if (executor->GetSimulation()->IsParentOrAncestor(a.actor_runtime(), b.actor_runtime()) ||
            executor->GetSimulation()->IsParentOrAncestor(b.actor_runtime(), a.actor_runtime())) {
            return;
        }

        // Skip collision between two static actors
        /*if (a.actor_spec().has_miscellaneous_spec() && b.actor_spec().has_miscellaneous_spec())
        {
                return;
        }*/

        // Compute oriented bounding box based on current pose of the actors
        geometry::OrientedBox3<double> boxA = geometry::GetOrientedBoundingBox(a);
        geometry::OrientedBox3<double> boxB = geometry::GetOrientedBoundingBox(b);

        // If collision occurred, return the actors involved
        if (geometry::TIQuery(boxA, boxB)) {
            mCollisions.insert({&a, &b});
        }
    };

    // Reset the collision states
    mCollisions.clear();

    // Get all actors if needed
    std::vector<rrSP<sseProto::Actor>> allActors;
    if (mActorIds.empty() || mRefActorIds.empty()) {
        allActors = executor->GetSimulation()->GetActorsInternal();
    }

    if (mActorIds.empty() && mRefActorIds.empty()) {
        // Collision needs to be checked on any two actors
        size_t count = allActors.size();
        for (size_t i = 0; i < count; ++i) {
            // We apply an optimization (i.e., j = i + 1) here to eliminate
            // repetitive checks
            for (size_t j = i + 1; j < count; ++j) {
                checkCollision(*allActors[i], *allActors[j]);
            }
        }
    } else {
        // Collision needs to be checked on specified actors

        // Initialize actor group 1
        std::vector<rrSP<sseProto::Actor>>* group1 = &allActors;
        std::vector<rrSP<sseProto::Actor>>  actors;
        if (!mActorIds.empty()) {
            group1 = &actors;
            for (const auto& id : mActorIds) {
                actors.push_back(executor->GetSimulation()->FindActor(id));
            }
        }

        // Initialize actor group 2
        std::vector<rrSP<sseProto::Actor>>* group2 = &allActors;
        std::vector<rrSP<sseProto::Actor>>  refActors;
        if (!mRefActorIds.empty()) {
            group2 = &refActors;
            for (const auto& id : mRefActorIds) {
                refActors.push_back(executor->GetSimulation()->FindActor(id));
            }
        }

        // Find actors that are in both groups. Optimization will be applied to
        // eliminate costly repetitive checks.
        std::set<rrSP<sseProto::Actor>> repeats;
        std::set<rrSP<sseProto::Actor>> uniques;
        uniques.insert(group1->begin(), group1->end());
        for (const auto& actor2 : *group2) {
            auto result = uniques.insert(actor2);
            if (!result.second) {
                // Found an actor in group2 that is also in group1
                repeats.insert(actor2);
            }
        }

        // Track collisions that has already been checked to avoid repeat
        std::set<std::pair<rrSP<sseProto::Actor>, rrSP<sseProto::Actor>>> skipSet;

        // Detect collisions
        for (auto& actor1 : *group1) {
            for (auto& actor2 : *group2) {
                // Skip collision check from actor1 to actor2 if
                // - actor1 and actor2 are the same actor
                // - actor2 to actor1 check has already been done
                if (actor1 == actor2 || skipSet.find({actor1, actor2}) != skipSet.end()) {
                    continue;
                }

                if (repeats.find(actor1) != repeats.end() &&
                    repeats.find(actor2) != repeats.end()) {
                    // Both actors appear in group1 and group2. For example,
                    // 2 and 3 both appear in { 1 2 3 } { 2 3 4 }. We should
                    // do check 2->3 and skip 3->2 to avoid repeat.
                    skipSet.insert({actor2, actor1});
                }
                checkCollision(*actor1, *actor2);
            }
        }
    }

    mState = !mCollisions.empty();
    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////

void CollisionCondition::GetConditionStatus(sseProto::ConditionStatus* rtCondition) {
    // Get runtime condition for parent object
    SingularCondition::GetConditionStatus(rtCondition);
    // Get runtime condition specific to collisions
    auto* rtCollisions = rtCondition->mutable_collision_condition_status();
    for (const auto& [actor, other_actor] : mCollisions) {
        auto* collision = rtCollisions->add_collisions();
        collision->set_actor_id(actor->actor_spec().id());
        collision->set_other_actor(other_actor->actor_spec().id());
    }
}

////////////////////////////////////////////////////////////////////////////////

std::string CollisionCondition::GetSummary() const {
    if (mState.has_value()) {
        if (mState.value()) {
            // Collision has occurred
            std::ostringstream ss;
            for (const auto& [actor, other_actor] : mCollisions) {
                std::string str = ("\nCollision occurred between: " + actor->actor_spec().id() + " and " +
                                   other_actor->actor_spec().id());
                ss << str;
            }
            return ss.str();
        } else {
            // No collision detected
            return "The collision condition was evaluated, and no collision has been detected";
        }
    } else {
        return "The collision condition has not yet been evaluated";
    }
}

////////////////////////////////////////////////////////////////////////////////
// ActorSpeedCondition
////////////////////////////////////////////////////////////////////////////////

ActorSpeedCondition::ActorSpeedCondition(
    const std::string&           actorId,
    const sseProto::SpeedTarget& speedTarget)
    : SingularCondition()
    , mActorId(actorId)
    , mSpeedReference(speedTarget.speed_reference())
    , mComparisonRule(mSpeedReference.rule()) {
    rrThrowVerify(mSpeedReference.speed_comparison() != sseProto::SpeedComparison::SPEED_COMPARISON_UNSPECIFIED,
                  "Invalid speed comparison type.");
    rrThrowVerify(mSpeedReference.speed_comparison() == sseProto::SpeedComparison::SPEED_COMPARISON_ABSOLUTE ||
                      mSpeedReference.reference_sampling_mode() != sseProto::ReferenceSamplingMode::REFERENCE_SAMPLING_MODE_UNSPECIFIED,
                  "Invalid sampling mode for speed reference.");

    // - For equality comparison relative to a reference actor, the target min
    //   and max speed are both zero (default)
    // - For other types of comparison, target speed are on the speed value
    if (mSpeedReference.speed_comparison() != sseProto::SpeedComparison::SPEED_COMPARISON_SAME_AS) {
        if (speedTarget.has_range()) {
            mMin = speedTarget.range().min();
            mMax = speedTarget.range().max();
        } else {
            mMin = speedTarget.value();
            mMax = mMin;
        }
    }
    rrThrowVerify(mMin <= mMax,
                  "Lower bound value must be less than or equal to upper bound value in a speed range");
}

////////////////////////////////////////////////////////////////////////////////

void ActorSpeedCondition::Apply(ScenarioExecutor* executor) {
    if (mSpeedReference.reference_sampling_mode() == sseProto::ReferenceSamplingMode::REFERENCE_SAMPLING_MODE_AT_ACTION_START &&
        IsRelative(mSpeedReference.speed_comparison())) {
        mRefSpeed = GetActorSpeed(executor, mSpeedReference.reference_actor_id());
    }
}

////////////////////////////////////////////////////////////////////////////////

bool ActorSpeedCondition::Check(ScenarioExecutor* executor) {
    // Get speed of this actor
    mSpeed = GetActorSpeed(executor, mActorId);

    // If applicable, get speed of reference actor
    sseProto::SpeedComparison refType = mSpeedReference.speed_comparison();
    if (mSpeedReference.reference_sampling_mode() ==
            sseProto::ReferenceSamplingMode::REFERENCE_SAMPLING_MODE_CONTINUOUS &&
        IsRelative(refType)) {
        mRefSpeed = GetActorSpeed(executor, mSpeedReference.reference_actor_id());
    }

    // Deduce evaluation speed using speed comparison
    double pivot;
    switch (refType) {
    case sseProto::SpeedComparison::SPEED_COMPARISON_ABSOLUTE:
        // Absolute speed
        pivot = mSpeed;
        break;
    case sseProto::SpeedComparison::SPEED_COMPARISON_SAME_AS:
    case sseProto::SpeedComparison::SPEED_COMPARISON_FASTER_THAN:
        // Relative speed (equal or faster)
        pivot = mSpeed - mRefSpeed;
        break;
    case sseProto::SpeedComparison::SPEED_COMPARISON_SLOWER_THAN:
        // Relative speed (slower)
        pivot = mRefSpeed - mSpeed;
        break;
    default:
        rrThrowVerify(false, "Invalid speed reference type.");
    }
    double tolerance = ScenarioConfiguration::GetSpeedComparisonTolerance();

    // Apply comparison rule to evaluate state
    switch (mComparisonRule) {
    case sseProto::ComparisonRule::COMPARISON_RULE_GREATER_OR_EQUAL:
        mState = pivot >= (mMax - tolerance);
        break;
    case sseProto::ComparisonRule::COMPARISON_RULE_GREATER_THAN:
        mState = pivot > (mMax + tolerance);
        break;
    case sseProto::ComparisonRule::COMPARISON_RULE_LESS_OR_EQUAL:
        mState = pivot <= (mMin + tolerance);
        break;
    case sseProto::ComparisonRule::COMPARISON_RULE_LESS_THAN:
        mState = pivot < (mMin - tolerance);
        break;
    case sseProto::ComparisonRule::COMPARISON_RULE_NOT_EQUAL_TO:
        mState = (pivot < (mMin - tolerance)) ||
                 (pivot > (mMax + tolerance));
        break;
    case sseProto::ComparisonRule::COMPARISON_RULE_EQUAL_TO:
    default:
        mState = (pivot >= (mMin - tolerance)) &&
                 (pivot <= (mMax + tolerance));
        break;
    }

    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////

void ActorSpeedCondition::GetConditionStatus(sseProto::ConditionStatus* rtCondition) {
    SingularCondition::GetConditionStatus(rtCondition);
    rtCondition->mutable_actor_speed_condition_status()->set_actor_speed(mSpeed);
}

////////////////////////////////////////////////////////////////////////////////

std::string ActorSpeedCondition::GetSummary() const {
    using namespace std;
    if (mState.has_value()) {
        if (mState.value()) {
            return ("Actor runtime speed: " + to_string(mSpeed) + " meets the speed condition [ " + to_string(mMin) + ", " + to_string(mMax) + " ].");
        } else {
            return ("Actor runtime speed: " + to_string(mSpeed) +
                    " has not met the speed condition [ " + to_string(mMin) + ", " + to_string(mMax) + " ].");
        }
    } else {
        return "Actor speed condition was not evaluated.";
    }
}

////////////////////////////////////////////////////////////////////////////////

void ActorSpeedCondition::Enforce(ScenarioExecutor* executor, sseProto::Actor& actor) const {
    // If applicable, get speed of reference actor
    double                    refSpeed = 0;
    sseProto::SpeedComparison refType  = mSpeedReference.speed_comparison();
    if (IsRelative(refType)) {
        refSpeed = GetActorSpeed(executor, mSpeedReference.reference_actor_id());
    }

    // Compute target speed
    double targetSpeed = 0;
    switch (refType) {
    case sseProto::SpeedComparison::SPEED_COMPARISON_ABSOLUTE:
        targetSpeed = (mMin + mMax) / 2.0;
        // Absolute speed
        break;
    case sseProto::SpeedComparison::SPEED_COMPARISON_SAME_AS:
        // Relative speed (equal)
        targetSpeed = refSpeed;
        break;
    case sseProto::SpeedComparison::SPEED_COMPARISON_FASTER_THAN:
        // Relative speed (faster)
        targetSpeed = refSpeed + (mMin + mMax) / 2.0;
        break;
    case sseProto::SpeedComparison::SPEED_COMPARISON_SLOWER_THAN:
        // Relative speed (slower)
        targetSpeed = refSpeed - (mMin + mMax) / 2.0;
        break;
    default:
        rrThrowVerify(false, "Invalid speed reference type.");
    }

    // Set target speed
    Util::SetVelocity(actor, targetSpeed);
}

////////////////////////////////////////////////////////////////////////////////

void ActorSpeedCondition::AddDependency(std::vector<std::string>& dependencies) const {
    dependencies.push_back(mActorId);

    sseProto::SpeedComparison refType = mSpeedReference.speed_comparison();
    if (IsRelative(refType)) {
        dependencies.push_back(mSpeedReference.reference_actor_id());
    }
}

////////////////////////////////////////////////////////////////////////////////

void ActorSpeedCondition::AddDependencyOfApply(std::vector<std::string>& dependencies) const {
    if (mSpeedReference.reference_sampling_mode() ==
            sseProto::ReferenceSamplingMode::REFERENCE_SAMPLING_MODE_AT_ACTION_START &&
        IsRelative(mSpeedReference.speed_comparison())) {
        dependencies.push_back(mSpeedReference.reference_actor_id());
    }
}

////////////////////////////////////////////////////////////////////////////////

double ActorSpeedCondition::GetActorSpeed(
    ScenarioExecutor*  executor,
    const std::string& actorId) const {
    rrSP<sseProto::Actor> actor = executor->GetSimulation()->FindActor(actorId);
    return Util::GetSpeed(*actor);
}

////////////////////////////////////////////////////////////////////////////////
// ActorLaneCondition
////////////////////////////////////////////////////////////////////////////////

ActorLaneCondition::ActorLaneCondition(
    const std::string&             actorId,
    int                            min,
    int                            max,
    const sseProto::LaneReference& rule)
    : SingularCondition()
    , mActorId(actorId)
    , mMin(min)
    , mMax(max)
    , mRule(rule) {
    rrThrowVerify(min <= max,
                  "Lower bound value must be less than or equal to upper bound value in a lane range");
    rrThrowVerify(rule.lane_comparison() != sseProto::LaneComparison::LANE_COMPARISON_UNSPECIFIED,
                  "Invalid lane comparison type.");
    switch (mRule.lane_comparison()) {
    case sseProto::LaneComparison::LANE_COMPARISON_LEFT_OF:
        mSide = rrSide::eLeft;
        break;
    case sseProto::LaneComparison::LANE_COMPARISON_RIGHT_OF:
        mSide = rrSide::eRight;
        break;
    default:
        VZ_NO_OP();
    }
}

////////////////////////////////////////////////////////////////////////////////

void ActorLaneCondition::Apply(ScenarioExecutor* executor) {
    mTargetLocations = GetActorMapLocations(executor, mRule.reference_actor_id());
}

////////////////////////////////////////////////////////////////////////////////

bool ActorLaneCondition::Check(ScenarioExecutor* executor) {
    rrSP<sseProto::Actor> egoActor = executor->GetSimulation()->FindActor(mActorId);
    mState                         = true;

    /*try
            {
                    auto distance = ScenarioConfiguration::GetMaxSearchDistance();
                    roadrunner::hdmap::LaterallyConnectedLanesOptions options(mMin, mMax, mSide, distance);
                    const auto egoActorLocations = roadrunner::hdmap::MapUtil::GetMapLocations(*egoActor);

                    if (egoActorLocations.HasOnLane() && mTargetLocations.HasOnLane())
                    {
                            // Query map service to evaluate lane adjacency
                            mState = executor->GetSimulation()->GetMapService()
                                    .EvaluateLaneCondition(mTargetLocations, *egoActor, options);
                    }
                    else
                    {
                            mState = false;
                    }

            }
            catch (const roadrunner::hdmap::MapAwareActionException& exp)
            {
                    switch (exp.mReason)
                    {
                    case roadrunner::hdmap::ExceptionType::eLoopingBackLaneSequence:
                            if (!mLoopbackConditionDetected)
                                    rrWarning("Swirl-shape lane sequence detected while evaluating lane change for " + egoActor->actor_spec().id() +  ".\
Try reducing MaxSearchDistance in SimulationConfiguration.xml.");
                            mLoopbackConditionDetected = true;
                            mState = false;
                            break;
                    default:
                            mState = false;
                            break;
                    }
            }*/

    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////

void ActorLaneCondition::GetConditionStatus(sseProto::ConditionStatus* rtCondition) {
    SingularCondition::GetConditionStatus(rtCondition);
    rtCondition->mutable_actor_lane_condition_status();
}

////////////////////////////////////////////////////////////////////////////////

void ActorLaneCondition::AddDependency(std::vector<std::string>& dependencies) const {
    dependencies.push_back(mActorId);
    if (mRule.reference_actor_id() != mActorId) {
        dependencies.push_back(mRule.reference_actor_id());
    }
}

////////////////////////////////////////////////////////////////////////////////
// ActorLateralOffsetCondition
////////////////////////////////////////////////////////////////////////////////

ActorLateralOffsetCondition::ActorLateralOffsetCondition(const std::string& actorId,
                                                         double             offsetValue)
    : SingularCondition()
    , mActorId(actorId)
    , mOffsetValue(offsetValue) {}

////////////////////////////////////////////////////////////////////////////////

void ActorLateralOffsetCondition::Apply(ScenarioExecutor* executor) {
    try {
        const auto* path = executor->FindPath(mActorId);
        rrThrowVerify(path->points_size() != 0, "");
        mPath.emplace();
        for (int i = 0; i < path->points_size(); ++i) {
            mPath.value().AddPoint(rrConvert<geometry::Vector3<double>>(path->points(i)), 0.0);
        }
        mPath.value().ParametrizeArcLength();
    } catch (const rrException&) {
        mTargetMapLocation = GetActorMapLocations(executor, mActorId);
    }
}

////////////////////////////////////////////////////////////////////////////////

bool ActorLateralOffsetCondition::Check(ScenarioExecutor* executor) {
    rrSP<sseProto::Actor> actor = executor->GetSimulation()->FindActor(mActorId);

    try {
        if (mTargetMapLocation.has_value()) {
            /*if (mTargetMapLocation.value().HasOnLane())
            {
                    // Query map service to compute and evaluate the lateral offset condition
                    auto distance = ScenarioConfiguration::GetMaxSearchDistance();
                    mState = executor->GetSimulation()->GetMapService()
                            .EvaluateLateralOffsetCondition(mTargetMapLocation.value(), *actor, distance, mOffsetValue);
            }
            else
            {
                    mState = false;
            }*/
            mState = false;
        } else if (mPath.has_value()) {
            /*mState = executor->GetSimulation()->GetMapService()
                    .EvaluateLateralOffsetCondition(mPath.value(), *actor, mOffsetValue);*/
            mState = false;
        }
    } catch (const std::exception& exp) {
        /*switch (exp.mReason)
        {
        case roadrunner::hdmap::ExceptionType::eLoopingBackLaneSequence:
                if (!mLoopbackConditionDetected)
                        rrWarning("Swirl-shape lane sequence detected while evaluating lateral offset for "+ actor->actor_spec().id() +".\
Try reducing MaxSearchDistance in SimulationConfiguration.xml.");
                mLoopbackConditionDetected = true;
                mState = false;
                break;
        default:
                mState = false;
                break;
        }*/
        rrError(exp.what());
    }

    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////

void ActorLateralOffsetCondition::GetConditionStatus(sseProto::ConditionStatus* rtCondition) {
    SingularCondition::GetConditionStatus(rtCondition);
    rtCondition->lateral_offset_condition_status();
}

////////////////////////////////////////////////////////////////////////////////

void ActorLateralOffsetCondition::AddDependency(std::vector<std::string>& dependencies) const {
    dependencies.push_back(mActorId);
}

////////////////////////////////////////////////////////////////////////////////

namespace {
////////////////////////////////////////////////////////////////////////////////
// Converts both strings to the given type
//	- Returns nullopt if either fails
template <typename TargetT>
rrOpt<std::pair<TargetT, TargetT>> TryConvertBoth(const std::string& first, const std::string& second) {
    try {
        return std::make_pair(rrConvert<TargetT>(first), rrConvert<TargetT>(second));
    } catch (const rrException&) {
        return {};
    }
}

////////////////////////////////////////////////////////////////////////////////

template <typename T>
bool CompareValues(const std::pair<T, T>& values, sseProto::ComparisonRule rule) {
    switch (rule) {
    case sseProto::ComparisonRule::COMPARISON_RULE_EQUAL_TO:
        return (values.first == values.second);
    case sseProto::ComparisonRule::COMPARISON_RULE_GREATER_OR_EQUAL:
        return (values.first >= values.second);
    case sseProto::ComparisonRule::COMPARISON_RULE_GREATER_THAN:
        return (values.first > values.second);
    case sseProto::ComparisonRule::COMPARISON_RULE_LESS_OR_EQUAL:
        return (values.first <= values.second);
    case sseProto::ComparisonRule::COMPARISON_RULE_LESS_THAN:
        return (values.first < values.second);
    case sseProto::ComparisonRule::COMPARISON_RULE_NOT_EQUAL_TO:
        return (values.first != values.second);
    default:
        rrThrow("Invalid comparison rule option");
    }
}

template <typename T>
bool EpsilonCompareValues(const std::pair<T, T>& values, sseProto::ComparisonRule rule, double tolerance) {
    switch (rule) {
    case sseProto::ComparisonRule::COMPARISON_RULE_EQUAL_TO:
        return EpsilonEqual(values.first, values.second, tolerance);
    case sseProto::ComparisonRule::COMPARISON_RULE_GREATER_OR_EQUAL:
        return (values.first >= values.second - tolerance);
    case sseProto::ComparisonRule::COMPARISON_RULE_GREATER_THAN:
        return (values.first > values.second - tolerance);
    case sseProto::ComparisonRule::COMPARISON_RULE_LESS_OR_EQUAL:
        return (values.first <= values.second + tolerance);
    case sseProto::ComparisonRule::COMPARISON_RULE_LESS_THAN:
        return (values.first < values.second + tolerance);
    case sseProto::ComparisonRule::COMPARISON_RULE_NOT_EQUAL_TO:
        return !EpsilonEqual(values.first, values.second, tolerance);
    default:
        rrThrow("Invalid comparison rule option");
    }
}
} // namespace

////////////////////////////////////////////////////////////////////////////////
// ParameterComparisonCondition
////////////////////////////////////////////////////////////////////////////////

rrOpt<roadrunner::proto::Parameter> ParameterComparisonCondition::FindParameter(
    const google::protobuf::RepeatedPtrField<sseProto::Attribute>& parameterCollection,
    const std::string&                                             parameterName) {
    for (int idx = 0; idx < parameterCollection.size(); ++idx) {
        roadrunner::proto::Parameter param{parameterCollection[idx]};
        if (param.GetParameterNameFromProto() == parameterName) {
            return param;
        }
    }
    return std::nullopt;
};

////////////////////////////////////////////////////////////////////////////////

bool ParameterComparisonCondition::Compare(
    const std::unordered_map<std::string, roadrunner::proto::Parameter>& parameterNameValueMap,
    double                                                               tolerance) {
    bool allParamsSatisfied = true;
    for (const auto& [name, value] : parameterNameValueMap) {
        auto iter = mParameterExpressionsMap.find(name);

        if (iter == mParameterExpressionsMap.end()) {
            mState = false;
            return mState.value();
        }

        auto               rule           = iter->second.mRule;
        const std::string& comparedValStr = (iter->second.mComparedValue);
        switch (value.GetParameterDataTypeFromProto()) {
        case roadrunner::proto::EnumParameterDataType::eDouble: {
            using value_type       = double;
            value_type currentVal  = value.GetParameterValueFromProto().number_element().double_element();
            value_type comparedVal = rrConvert<value_type>(comparedValStr);
            allParamsSatisfied &= EpsilonCompareValues(std::make_pair(currentVal, comparedVal), rule, tolerance);
            break;
        }
        case roadrunner::proto::EnumParameterDataType::eUint16: {
            using value_type       = uint16_t;
            value_type currentVal  = value.GetParameterValueFromProto().number_element().uint16_element();
            value_type comparedVal = rrConvert<value_type>(comparedValStr);
            allParamsSatisfied &= CompareValues(std::make_pair(currentVal, comparedVal), rule);
            break;
        }
        case roadrunner::proto::EnumParameterDataType::eUint32: {
            using value_type       = uint32_t;
            value_type currentVal  = value.GetParameterValueFromProto().number_element().uint32_element();
            value_type comparedVal = rrConvert<value_type>(comparedValStr);
            allParamsSatisfied &= CompareValues(std::make_pair(currentVal, comparedVal), rule);
            break;
        }
        case roadrunner::proto::EnumParameterDataType::eInt32: {
            using value_type       = int32_t;
            value_type currentVal  = value.GetParameterValueFromProto().number_element().int32_element();
            value_type comparedVal = rrConvert<value_type>(comparedValStr);
            allParamsSatisfied &= CompareValues(std::make_pair(currentVal, comparedVal), rule);
            break;
        }
        case roadrunner::proto::EnumParameterDataType::eBoolean: {
            using value_type       = bool;
            value_type currentVal  = value.GetParameterValueFromProto().logical_element();
            value_type comparedVal = rrConvert<value_type>(comparedValStr);
            allParamsSatisfied &= CompareValues(std::make_pair(currentVal, comparedVal), rule);
            break;
        }
        case roadrunner::proto::EnumParameterDataType::eDateTime:
        case roadrunner::proto::EnumParameterDataType::eString: {
            using value_type      = std::string;
            value_type currentVal = value.GetParameterValueFromProto().string_element();
            allParamsSatisfied &= CompareString(currentVal, comparedValStr, rule, tolerance);
            break;
        }
        default:
            rrThrow("Invalid parameter data type for parameter: " + name);
            break;
        }
    }

    mState = allParamsSatisfied;
    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////

bool ParameterComparisonCondition::CompareString(const std::string& val, const std::string& comparedVal, sseProto::ComparisonRule rule, double tolerance) {
    const auto& valStr         = val;
    const auto& comparedValStr = comparedVal;

    if (const auto& doubles = TryConvertBoth<double>(valStr, comparedValStr)) {
        return EpsilonCompareValues(*doubles, rule, tolerance);
    } else if (const auto& bools = TryConvertBoth<bool>(valStr, comparedValStr)) {
        return CompareValues(*bools, rule);
    }
    /*else if (const auto& dates = TryConvertBoth<QDateTime>(valStr, comparedValStr))
            return CompareValues(*dates, rule);*/
    else {
        // Apply string values and rule
        switch (rule) {
        case sseProto::ComparisonRule::COMPARISON_RULE_EQUAL_TO:
            return (valStr == comparedValStr);
        case sseProto::ComparisonRule::COMPARISON_RULE_GREATER_OR_EQUAL:
        case sseProto::ComparisonRule::COMPARISON_RULE_GREATER_THAN:
        case sseProto::ComparisonRule::COMPARISON_RULE_LESS_OR_EQUAL:
        case sseProto::ComparisonRule::COMPARISON_RULE_LESS_THAN:
            rrThrow("Unable to convert string values to a scalar type for comparison (values: '" + valStr +
                    "' and '" + comparedValStr + "')");
            break;
        case sseProto::ComparisonRule::COMPARISON_RULE_NOT_EQUAL_TO:
            return (valStr != comparedValStr);
        default:
            rrThrow("Invalid comparison rule option.");
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

bool ParameterComparisonCondition::CompareBehaviorParameterForSimulinkAgent(
    const std::unordered_map<std::string, roadrunner::proto::Parameter>& parameterNameValueMap,
    double                                                               tolerance) {
    // Since we don't know the actual parameter types (as of R2022b), try various common conversions in order
    bool allParamsSatisfied = true;
    for (const auto& [name, value] : parameterNameValueMap) {
        rrThrowVerify(value.GetParameterDataTypeFromProto() == roadrunner::proto::EnumParameterDataType::eString,
                      "Encountered invalid data type for Parameter " + name);

        auto iter = mParameterExpressionsMap.find(name);

        if (iter == mParameterExpressionsMap.end()) {
            mState = false;
            return mState.value();
        }

        const auto& valStr         = value.GetParameterValueFromProto().string_element();
        const auto& comparedValStr = iter->second.mComparedValue;
        auto        rule           = iter->second.mRule;
        allParamsSatisfied &= CompareString(valStr, comparedValStr, rule, tolerance);
    }

    mState = allParamsSatisfied;
    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////
// CustomEventCondition
////////////////////////////////////////////////////////////////////////////////

CustomEventCondition::CustomEventCondition(const sseProto::EventCondition& proto)
    : mEventName(proto.event_name()) {
    rrThrowVerify(!mEventName.empty(),
                  "Invalid data model for an event condition. "
                  "A custom event must have a name.");

    for (int i = 0; i < proto.parameter_conditions_size(); ++i) {
        ParameterComparison paramCompare;
        auto                name    = proto.parameter_conditions()[i].parameter_reference().parameter_name();
        paramCompare.mComparedValue = proto.parameter_conditions()[i].compared_parameter_value();
        paramCompare.mRule          = proto.parameter_conditions()[i].rule();
        mParameterExpressionsMap.insert({name, paramCompare});
    }
}

////////////////////////////////////////////////////////////////////////////////

void CustomEventCondition::Apply(ScenarioExecutor* executor) {
    rrThrowVerify(executor->IsUserDefinedEventRegistered(mEventName),
                  "Undefined custom event in a " + mId);
}

////////////////////////////////////////////////////////////////////////////////

bool CustomEventCondition::Check(ScenarioExecutor* executor) {
    auto eventQueue = executor->GetUserDefinedEvents(mEventName);

    if (!eventQueue.has_value()) {
        mState = false;
        return mState.value();
    }

    for (const auto& customCommand : *eventQueue) {
        std::unordered_map<std::string, roadrunner::proto::Parameter> paramNameValMap;
        for (const auto& [name, unused] : mParameterExpressionsMap) {
            if (auto param = FindParameter(customCommand.attributes(), name)) {
                paramNameValMap.insert({name, *param});
            }
        }
        if (!Compare(paramNameValMap)) {
            continue;
        } else {
            return mState.value();
        }
    }

    mState = false;
    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////
// ParameterCondition
////////////////////////////////////////////////////////////////////////////////

ParameterCondition::ParameterCondition(const sseProto::ParameterCondition& proto)
    : mActorId(proto.parameter_reference().actor_id())
    , mParameterType(proto.parameter_reference().parameter_type()) {
    rrThrowVerify(!mActorId.empty(),
                  "Invalid actor ID for parameter condition. Actor ID is empty");
    rrThrowVerify(!proto.parameter_reference().parameter_name().empty(),
                  "Invalid parameter name for parameter condition. Parameter name is empty");
    // SSE should check for empty value for non-string parameter data types (e.g. double, int)
    // but since all parameters are converted to string so this check is commented out
    // to enable empty string value
    // - FUTURE: re-enable this check
    // rrThrowVerify(!mComparedValue.empty(),
    //	"Invalid parameter value for parameter condition. Parameter value is empty");
    rrThrowVerify(mParameterType != sseProto::ParameterType::PARAMETER_TYPE_UNSPECIFIED,
                  "Invalid parameter type for parameter condition");
    rrThrowVerify(proto.rule() != sseProto::ComparisonRule::COMPARISON_RULE_UNSPECIFIED,
                  "Invalid comparison rule for parameter condition");

    ParameterComparison paramCompare;
    auto                name    = proto.parameter_reference().parameter_name();
    paramCompare.mComparedValue = proto.compared_parameter_value();
    paramCompare.mRule          = proto.rule();
    mParameterExpressionsMap.insert({name, paramCompare});
}

////////////////////////////////////////////////////////////////////////////////

bool ParameterCondition::Check(ScenarioExecutor* executor) {
    // Find actor and report error if not found
    rrSP<sseProto::Actor>                                          actorPtr            = executor->GetSimulation()->FindActor(mActorId);
    const google::protobuf::RepeatedPtrField<sseProto::Attribute>* parameterCollection = nullptr;

    switch (mParameterType) {
    case sseProto::PARAMETER_TYPE_ACTOR:
        parameterCollection = &actorPtr->actor_runtime().parameters();
        break;
    case sseProto::PARAMETER_TYPE_BEHAVIOR:
        parameterCollection = &actorPtr->actor_runtime().behavior_parameters();
        break;
    default:
        rrThrow("Invalid parameter type for parameter condition");
        break;
    }

    std::unordered_map<std::string, roadrunner::proto::Parameter> paramNameValMap;
    for (const auto& [name, valUnused] : mParameterExpressionsMap) {
        if (auto param = FindParameter(*parameterCollection, name)) {
            paramNameValMap.insert({name, *param});
        } else {
            mState = false;
            return mState.value();
        }
    }

    auto behavior = executor->GetSimulation()->FindBehavior(mActorId, false);

    if (mParameterType == sseProto::PARAMETER_TYPE_BEHAVIOR &&
        behavior && behavior->has_simulink_behavior()) {
        return CompareBehaviorParameterForSimulinkAgent(paramNameValMap);
    } else {
        return Compare(paramNameValMap);
    }
}

////////////////////////////////////////////////////////////////////////////////
// ActionsCompleteCondition
////////////////////////////////////////////////////////////////////////////////

void ActionsCompleteCondition::Apply(ScenarioExecutor* executor) {
    mPhase = dynamic_cast<PhaseDiagramExecutor*>(executor)->FindPhase(mPhaseId);
}

////////////////////////////////////////////////////////////////////////////////

bool ActionsCompleteCondition::Check(ScenarioExecutor*) {
    mState = mPhase->HasCompleted();
    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////

std::string ActionsCompleteCondition::GetSummary() const {
    const auto& link = mPhaseId;
    if (mState.has_value()) {
        if (mState.value()) {
            return ("The " + link + " has completed.");
        } else {
            return ("The " + link + " has not completed.");
        }
    } else {
        return ("The " + link + " has not been evaluated.");
    }
}

////////////////////////////////////////////////////////////////////////////////
// TimeToActorCondition
////////////////////////////////////////////////////////////////////////////////

TimeToActorCondition::TimeToActorCondition(
    const std::string&              actorId,
    const sseProto::DistanceTarget& target)
    : DistanceCondition(target.value(), target.distance_reference().rule())
    , mActorId(actorId)
    , mRefActorId(target.distance_reference().reference_actor_id())
    , mMeasureFrom(target.distance_reference().measure_from())
    , mCoordinateSystem(target.distance_reference().actor_coordinate_system_type()) {
    rrThrowVerify(target.distance_type() == sseProto::DISTANCE_TYPE_TIME,
                  "Invalid distance type for time-to-collision condition. Distance type must be time.");
    rrThrowVerify(target.distance_dimension_type() == sseProto::DISTANCE_DIMENSION_TYPE_LONGITUDINAL,
                  "Invalid distance dimension type for time-to-collision condition. Distance dimension type must be longitudinal.");
    rrThrowVerify(mMeasureFrom != sseProto::MEASURE_FROM_UNSPECIFIED,
                  "Invalid measure from type for time-to-collision condition");
    rrThrowVerify(mCoordinateSystem != sseProto::ACTOR_COORDINATE_SYSTEM_TYPE_UNSPECIFIED,
                  "Invalid actor coordinate system type for time-to-collision condition");
}

////////////////////////////////////////////////////////////////////////////////

void TimeToActorCondition::Apply(ScenarioExecutor* /*executor*/) {
}

////////////////////////////////////////////////////////////////////////////////

bool TimeToActorCondition::Check(ScenarioExecutor* executor) {
    rrSP<sseProto::Actor> actor    = executor->GetSimulation()->FindActor(mActorId);
    rrSP<sseProto::Actor> refActor = executor->GetSimulation()->FindActor(mRefActorId);
    /*const auto& result = executor->GetSimulation()->GetMapService()
            .ComputeTimeToCollision(*actor, *refActor, mMeasureFrom, mCoordinateSystem);

    if (result.has_value())
    {
            mDistance = result->mValue;
            mRuntimeActorPoint = result->mFirstPoint;
            mRuntimeRefObjPoint = result->mSecondPoint;
    }
    else*/
    {
        mState = false;
        return mState.value();
    }

    Compare(mDistance, cDefaultTolerance);

    return mState.value();
}

////////////////////////////////////////////////////////////////////////////////

void TimeToActorCondition::AddDependency(std::vector<std::string>& dependencies) const {
    dependencies.push_back(mActorId);
    dependencies.push_back(mRefActorId);
}

////////////////////////////////////////////////////////////////////////////////

std::string TimeToActorCondition::GetSummary() const {
    if (mState.has_value()) {
        return ("Time-to-collision condition was met.");
    } else {
        return "Time-to-collision distance condition was not evaluated.";
    }
}

} // namespace sse
