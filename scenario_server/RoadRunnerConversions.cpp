#include "RoadRunnerConversions.h"

////////////////////////////////////////////////////////////////////////////////

void SetVelocity(sseProto::Actor& actor, double speed) {
    auto  actorRuntime = actor.mutable_actor_runtime();
    auto& heading      = actorRuntime->pose().matrix().col1();
    auto* v            = actorRuntime->mutable_velocity();
    v->set_x(speed * heading.x());
    v->set_y(speed * heading.y());
    v->set_z(speed * heading.z());
}

////////////////////////////////////////////////////////////////////////////////

namespace Util {
void SetVelocity(sseProto::ActorRuntime& actorRuntime, double speed) {
    auto& heading = actorRuntime.pose().matrix().col1();
    auto* v       = actorRuntime.mutable_velocity();
    v->set_x(speed * heading.x());
    v->set_y(speed * heading.y());
    v->set_z(speed * heading.z());
}

////////////////////////////////////////////////////////////////////////////////

void SetVelocity(sseProto::Actor& actor, double speed) {
    SetVelocity(*(actor.mutable_actor_runtime()), speed);
}

////////////////////////////////////////////////////////////////////////////////

double GetSpeed(const sseProto::ActorRuntime& actorRuntime) {
    auto& v = actorRuntime.velocity();

    // Compute the sign of speed based on vehicle's heading and velocity's direction
    auto&  heading           = actorRuntime.pose().matrix().col1();
    double projectedVelocity = v.x() * heading.x() + v.y() * heading.y() + v.z() * heading.z();
    double speed             = std::sqrt(v.x() * v.x() + v.y() * v.y() + v.z() * v.z());
    return (projectedVelocity >= 0) ? speed : -speed;
}

////////////////////////////////////////////////////////////////////////////////

double GetSpeed(const sseProto::Actor& actor) {
    return GetSpeed(actor.actor_runtime());
}

////////////////////////////////////////////////////////////////////////////////

void CheckSimulationStepSize(const double stepSize) {
    rrThrowVerify((stepSize >= cMinStepSize && stepSize <= cMaxStepSize),
                  "Simulation step size " + std::to_string(stepSize) + " is out of range: [" + std::to_string(cMinStepSize) + " " + std::to_string(cMaxStepSize) + "].");
}

////////////////////////////////////////////////////////////////////////////////

void CheckSimulationPace(const double pace) {
    rrThrowVerify((pace >= cPacingLowerBound && pace <= cPacingUpperBound),
                  "Simulation pace " + std::to_string(pace) + " is out of range: [" + std::to_string(cPacingLowerBound) + " " + std::to_string(cPacingUpperBound) + "].");
}

////////////////////////////////////////////////////////////////////////////////

void CheckMaxSimulationTime(const double simTime) {
    rrThrowVerify((simTime >= cMaxSimulationTimeLowerBound && simTime <= cMaxSimulationTimeUpperBound), "Maximum simulation time " + std::to_string(simTime) +
                                                                                                            " is out of range: [" + std::to_string(cMaxSimulationTimeLowerBound) + " " + std::to_string(cMaxSimulationTimeUpperBound) + "].");
}

////////////////////////////////////////////////////////////////////////////////

void SetWorldCoordinates(sseProto::ActorRuntime& actor, const sseProto::ActorRuntime& parent) {
    auto parentTrans = rrConvert<glm::dmat4>(parent.pose().matrix());

    // Convert pose
    auto pose_world = parentTrans *
                      rrConvert<glm::dmat4>(actor.local_pose().matrix());
    actor.mutable_pose()->mutable_matrix()->CopyFrom(
        rrConvert<sseProtoCommon::Matrix4x4>(pose_world));

    // Convert velocity
    parentTrans[3].x = parent.velocity().x();
    parentTrans[3].y = parent.velocity().y();
    parentTrans[3].z = parent.velocity().z();
    glm::vec4 v_local(
        actor.local_velocity().x(),
        actor.local_velocity().y(),
        actor.local_velocity().z(),
        1);
    auto v_world = parentTrans * v_local;
    actor.mutable_velocity()->set_x(v_world.x);
    actor.mutable_velocity()->set_y(v_world.y);
    actor.mutable_velocity()->set_z(v_world.z);

    // Convert angular velocity
    parentTrans[3].x = parent.angular_velocity().x();
    parentTrans[3].y = parent.angular_velocity().y();
    parentTrans[3].z = parent.angular_velocity().z();
    glm::vec4 av_local(
        actor.local_angular_velocity().x(),
        actor.local_angular_velocity().y(),
        actor.local_angular_velocity().z(),
        1);
    auto av_world = parentTrans * av_local;
    actor.mutable_angular_velocity()->set_x(av_world.x);
    actor.mutable_angular_velocity()->set_y(av_world.y);
    actor.mutable_angular_velocity()->set_z(av_world.z);
}

////////////////////////////////////////////////////////////////////////////////

void SetLocalCoordinates(sseProto::ActorRuntime& actor, const sseProto::ActorRuntime& parent) {
    auto parentTrans = rrConvert<glm::dmat4>(parent.pose().matrix());

    // Convert pose
    auto pose_local = glm::inverse(parentTrans) *
                      rrConvert<glm::dmat4>(actor.pose().matrix());
    actor.mutable_local_pose()->mutable_matrix()->CopyFrom(
        rrConvert<sseProtoCommon::Matrix4x4>(pose_local));

    // Convert velocity
    parentTrans[3].x = parent.velocity().x();
    parentTrans[3].y = parent.velocity().y();
    parentTrans[3].z = parent.velocity().z();
    glm::vec4 v_world(
        actor.velocity().x(),
        actor.velocity().y(),
        actor.velocity().z(),
        1);
    auto v_local = glm::inverse(parentTrans) * v_world;
    actor.mutable_local_velocity()->set_x(v_local.x);
    actor.mutable_local_velocity()->set_y(v_local.y);
    actor.mutable_local_velocity()->set_z(v_local.z);

    // Convert angular velocity
    parentTrans[3].x = parent.angular_velocity().x();
    parentTrans[3].y = parent.angular_velocity().y();
    parentTrans[3].z = parent.angular_velocity().z();
    glm::vec4 av_world(
        actor.angular_velocity().x(),
        actor.angular_velocity().y(),
        actor.angular_velocity().z(),
        1);
    auto av_local = glm::inverse(parentTrans) * av_world;
    actor.mutable_local_angular_velocity()->set_x(av_local.x);
    actor.mutable_local_angular_velocity()->set_y(av_local.y);
    actor.mutable_local_angular_velocity()->set_z(av_local.z);
}

} // namespace Util

namespace geometry {

double Dot(const Vector3<double>& a, const Vector3<double>& b) {
    return (a.getX() * b.getX() + a.getY() * b.getY() + a.getZ() * b.getZ());
}

////////////////////////////////////////////////////////////////////////////////

OrientedBox3<double> GetOrientedBoundingBox(const sseProto::Actor& actor) {
    // get the bounding box from actor spec
    const sseProtoCommon::Vector3& min = actor.actor_spec().bounding_box().min();
    const sseProtoCommon::Vector3& max = actor.actor_spec().bounding_box().max();

    // Get current actor pose
    const sseProtoCommon::Matrix4x4& matrix = actor.actor_runtime().pose().matrix();

    // Compute the center of the BBox for the static actor
    sseProtoCommon::Vector3 center;
    center.set_x((min.x() + max.x()) / 2.);
    center.set_y((min.y() + max.y()) / 2.);
    center.set_z((min.z() + max.z()) / 2.);

    // Translate the BBox center to the runtime actor location
    sseProtoCommon::Vector3 rtCenter;
    rtCenter.set_x(center.x() + matrix.col3().x());
    rtCenter.set_y(center.y() + matrix.col3().y());
    rtCenter.set_y(center.z() + matrix.col3().z());

    // Construct oriented box
    OrientedBox3<double> orientedBox;
    orientedBox.setCenter(rtCenter);
    orientedBox.setAxes(matrix);

    // Set bounding box min and max - calculate the current position (spec + current position)
    std::array<double, 3> bbmin = {(matrix.col3().x() + min.x() * (matrix.col0().x() + matrix.col1().x() + matrix.col2().x())),
                                   (matrix.col3().y() + min.y() * (matrix.col0().y() + matrix.col1().y() + matrix.col2().y())),
                                   (matrix.col3().z() + min.z() * (matrix.col0().z() + matrix.col1().z() + matrix.col2().z()))};

    std::array<double, 3> bbmax = {(matrix.col3().x() + max.x() * (matrix.col0().x() + matrix.col1().x() + matrix.col2().x())),
                                   (matrix.col3().y() + max.y() * (matrix.col0().y() + matrix.col1().y() + matrix.col2().y())),
                                   (matrix.col3().z() + max.z() * (matrix.col0().z() + matrix.col1().z() + matrix.col2().z()))};

    orientedBox.setMin(bbmin);
    orientedBox.setMax(bbmax);

    return orientedBox;
}

////////////////////////////////////////////////////////////////////////////////

bool InContainer(const std::array<double, 3>& point, const OrientedBox3<double>& box) {
    std::array<double, 3> center = box.getCenter();
    std::array<double, 3> diff   = {point[0] - center[0], point[1] - center[1], point[2] - center[2]};

    for (int i = 0; i < 3; ++i) {
        double dotProd = (diff[0] * box.getAxis(i)[0] + diff[1] * box.getAxis(i)[1] + diff[2] * box.getAxis(i)[2]);
        if (std::fabs(dotProd) > box.getExtents()[i]) {
            return false;
        }
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////

DCPQuery::Result DCPQuery::operator()(Vector3<double> const& point, OrientedBox3<double> const& box) const {
    // Express the point in the box's center frame
    std::array<double, 3> center            = box.getCenter();
    std::array<double, 3> boxExtent         = box.getExtents();
    Vector3<double>       boxCenter         = {center[0], center[1], center[2]};
    Vector3<double>       closestPointOnBox = point - boxCenter;

    Result result;
    ComputeClosestPoint(closestPointOnBox, boxExtent, result);

    // Compute the closest point on the box.
    result.closestPoint = boxCenter + closestPointOnBox;
    return result;
}

////////////////////////////////////////////////////////////////////////////////

void DCPQuery::ComputeClosestPoint(Vector3<double>& point, Vector3<double> const& boxExtent, Result& result) const {
    result.squareOfDistance = (double)0;
    for (int i = 0; i < 3; ++i) {
        // Does the point fall within any of the extents?
        if (point[i] < -boxExtent[i]) {
            double delta = point[i] + boxExtent[i];
            point[i]     = -boxExtent[i];
            result.squareOfDistance += delta * delta;
        } else if (point[i] > boxExtent[i]) {
            double delta = point[i] - boxExtent[i];
            point[i]     = boxExtent[i];
            result.squareOfDistance += delta * delta;
        }
    }
    // Compute the square root to get distance
    result.distance = std::sqrt(result.squareOfDistance);
}

////////////////////////////////////////////////////////////////////////////////

DCPQuery::Result DCPQueryAny(Vector3<double> const& point, OrientedBox3<double> const& box) {
    return DCPQuery{}(point, box);
}

////////////////////////////////////////////////////////////////////////////////

bool TIQuery(OrientedBox3<double> const& box0, OrientedBox3<double> const& box1) {
    auto box0Min = box0.getMin();
    auto box0Max = box0.getMax();
    auto box1Min = box1.getMin();
    auto box1Max = box1.getMax();

    // Axis aligned bbox intersection (does not work for oriented bbox, which is our case)
    bool hasIntersected =
        std::max(box0Min[0], box1Min[0]) <= std::min(box0Max[0], box1Max[0]) &&
        std::max(box0Min[1], box1Min[1]) <= std::min(box0Max[1], box1Max[1]) &&
        std::max(box0Min[2], box1Min[2]) <= std::min(box0Max[2], box1Max[2]);

    // Return false until we implement the oriented bbox intersection - g3064747
    return false;
}

////////////////////////////////////////////////////////////////////////////////

ParallelConfigurations::OrientedBox3OrientedBox3Result ParallelConfigurations::FindDistance(
    const geometry::OrientedBox3<double>& box1,
    const geometry::OrientedBox3<double>& box2) {
    OrientedBox3OrientedBox3Result result;

    std::array<Vector3<double>, 8> vertices1;
    box1.GetVertices(vertices1);
    std::array<Vector3<double>, 8> vertices2;
    box2.GetVertices(vertices2);

    double                         minDistance = DBL_MAX;
    std::array<Vector3<double>, 2> points;
    for (const auto& vertex1 : vertices1) {
        for (const auto& vertex2 : vertices2) {
            auto   delta           = vertex2 - vertex1;
            double currentDistance = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
            if (currentDistance < minDistance) {
                minDistance = currentDistance;
                points[0]   = vertex1;
                points[1]   = vertex2;
            }
        }
    }

    result.mDistance  = minDistance;
    result.mPoints[0] = points[0];
    result.mPoints[1] = points[1];

    return result;
}

////////////////////////////////////////////////////////////////////////////////

bool ParallelConfigurations::CheckParallel(const OrientedBox3<double>& box1, const OrientedBox3<double>& box2) {
    // From RoadRunner code base
    // Check the pair of x-x axes, and y-y axes for alignment. The x-y axes pair
    // is not considered as parallel configurations
    auto box1axisX = box1.getAxis(0);
    auto box1axisY = box1.getAxis(1);

    auto box2axisX = box1.getAxis(0);
    auto box2axisY = box1.getAxis(1);

    auto xxDotProduct = std::abs(Dot(box1axisX, box2axisX));
    auto xyDotProduct = std::abs(Dot(box1axisX, box2axisY));
    auto yyDotProduct = std::abs(Dot(box1axisY, box2axisY));

    // Use cFloatEpsilon tolerance for now, we might need to increase it in the future
    // Note: for orthogonal boxes (xy dot product), the threshold is expanded due
    // to poor numerical errors from the GTE
    return ((xxDotProduct >= 1.0 - cFloatEpsilon && yyDotProduct >= 1.0 - cFloatEpsilon) || xyDotProduct >= 0.99 - cFloatEpsilon) ? true : false;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace geometry
