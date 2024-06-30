#include "Util.h"
#include "ScenarioUtils.h"

////////////////////////////////////////////////////////////////////////////////

namespace {
// Pretty annoying - 'GetMessage' is defined as a macro in a windows header.
// See - https://stackoverflow.com/questions/1543736/how-do-i-temporarily-disable-a-macro-expansion-in-c-c
// For discussion on the solution.
//
const google::protobuf::Message& ExtractMessage(const google::protobuf::Reflection*      reflection,
                                                const google::protobuf::FieldDescriptor* fieldDescriptor,
                                                const google::protobuf::Message&         owningMessage) {
#pragma push_macro("GetMessage")
#undef GetMessage
    const auto& message = reflection->GetMessage(owningMessage, fieldDescriptor);
#pragma pop_macro("GetMessage")

    return message;
}
} // namespace

////////////////////////////////////////////////////////////////////////////////

namespace rrProtoGrpc {

////////////////////////////////////////////////////////////////////////////////


std::future<void> Util::StartServer(rrUP<grpc::Server>& outServer, grpc::ServerBuilder& builder, const std::string& address) {
    outServer = builder.BuildAndStart();
    rrThrowVerify(outServer, "Unable to start server on: " + address);

    return std::async(std::launch::async, [&outServer] {
        outServer->Wait();
    });
}

/*
    ////////////////////////////////////////////////////////////////////////////////

    void Util::CheckResult(const grpc::Status& status)
    {
            if (status.ok())
                    return;

            const auto code = status.error_code();
            QString displayMessage = "Command Failure (code %1): "_Q % (int)code;

            if (const auto& message = status.error_message(); !message.empty())
                    displayMessage += rrToStr(message);
            else
            {
                    //? TODO: Is there a standard way to map codes to messages?
                    if (code == grpc::StatusCode::UNIMPLEMENTED)
                    {
                            // Can occur when connecting to an outdated server, or if the server didn't start
                            //	all relevant services (e.g. if experimental service wasn't started)
                            displayMessage = "Unrecognized server command"_Q;
                    }
                    else
                            displayMessage += "No further details"_Q;
            }

            rrThrow(displayMessage);
    }

    ////////////////////////////////////////////////////////////////////////////////

    std::string Util::GetCoSimServerAddress()
    {
            return CreateLocalHostAddress(Ports::CoSimPort()).toStdString();
    }

    ////////////////////////////////////////////////////////////////////////////////

    QString Util::CreateLocalHostAddress(int port)
    {
            return CreateAddress("localhost"_Q, port);
    }

    ////////////////////////////////////////////////////////////////////////////////
*/

std::string Util::CreateAddress(const std::string& hostname, int port) {
    return hostname + ":" + std::to_string(port);
}

////////////////////////////////////////////////////////////////////////////////

/*
    roadrunner::core::IntRestriction Util::PortRangeRestriction()
    {
            return roadrunner::core::IntegerRestriction<int>(cMinPort, cMaxPort);
    }

    ////////////////////////////////////////////////////////////////////////////////

    void Util::InitContext(grpc::ClientContext& inoutContext, const std::string& clientID)
{
    // Add metadata to the context {Key = "client_id", Value = clientID}
    inoutContext.AddMetadata("client_id", clientID);
    inoutContext.set_deadline(std::chrono::system_clock::now() + GetDefaultRPCTimeout());
}

    ////////////////////////////////////////////////////////////////////////////////

    std::chrono::time_point<std::chrono::system_clock> Util::GetDefaultRPCDeadline()
    {
            return std::chrono::system_clock::now() + GetDefaultRPCTimeout();
    }

    ////////////////////////////////////////////////////////////////////////////////

    std::chrono::milliseconds Util::GetDefaultRPCTimeout()
    {
            // The RR App may hang for this amount of time, so we don't want to choose a large value,
            // however the gRPC server can (in rare cases) take up to 2 seconds to initialize fully, so
            // we want a value larger than that. Based on those considerations, I (Clayton) chose 5 seconds.
            return std::chrono::milliseconds(5000);
    }

    ////////////////////////////////////////////////////////////////////////////////

    double Util::GetDefaultRPCTimeoutSeconds()
    {
            return GetDefaultRPCTimeout().count() / 1000.0;
    }
*/

////////////////////////////////////////////////////////////////////////////////

const google::protobuf::Message& rrProtoGrpc::Util::GetOneofMessage(
    const google::protobuf::Message&         parentMessage,
    const google::protobuf::OneofDescriptor& oneofDescriptor,
    rrOpt<int>                               expectedCase) {
    const auto& reflection = *parentMessage.GetReflection();
    const auto* oneofFieldDescriptor =
        reflection.GetOneofFieldDescriptor(parentMessage, &oneofDescriptor);

    rrThrowVerify(oneofFieldDescriptor, "Field '" + oneofDescriptor.name() + "' has not been set.");
    rrThrowVerify(!expectedCase || *expectedCase == oneofFieldDescriptor->number(),
                  "Field '" + oneofDescriptor.name() + "' has an unexpected case.");
    return ExtractMessage(&reflection, oneofFieldDescriptor, parentMessage);
}

////////////////////////////////////////////////////////////////////////////////

bool operator==(const sseProtoCommon::EngineeringCoordinateSystem& a,
                const sseProtoCommon::EngineeringCoordinateSystem& b) {
    const auto& a_origin = a.geodetic_origin();
    const auto& b_origin = b.geodetic_origin();

    return (a_origin.latitude() == b_origin.latitude() &&
            a_origin.longitude() == b_origin.longitude() &&
            a_origin.altitude() == b_origin.altitude());
}

////////////////////////////////////////////////////////////////////////////////

bool operator==(const sseProtoCommon::CoordinateReferenceSystem& a,
                const sseProtoCommon::CoordinateReferenceSystem& b) {
    switch (a.type_case()) {
    case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kProjectedCoordinateSystem:
        return (b.has_projected_coordinate_system() &&
                (a.projected_coordinate_system().projection() == b.projected_coordinate_system().projection()));
    case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeographicCoordinateSystem:
        return (b.has_geographic_coordinate_system());
    case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeocentricCoordinateSystem:
        return (b.has_geocentric_coordinate_system());
    case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kEngineeringCoordinateSystem:
        return (b.has_engineering_coordinate_system() &&
                a.engineering_coordinate_system() == b.engineering_coordinate_system());
    default:
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////////

void Util::ConvertPoseEcefToEng(
    const sseProto::ActorPose& ecefPose,
    sseProto::ActorPose&       engPose) {
    const auto& origin = engPose.coordinate_system_ref().coordinate_system().engineering_coordinate_system().geodetic_origin();

    auto transEng2Ecef = mpProjectionUtil::GetLLA2ECEFTransform(origin.latitude(),
                                                                origin.longitude(), origin.altitude());

    // Convert pose
    auto pose_eng = glm::inverse(transEng2Ecef) *
                    rrConvert<glm::dmat4>(ecefPose.pose().matrix());
    engPose.mutable_pose()->mutable_matrix()->CopyFrom(
        rrConvert<sseProtoCommon::Matrix4x4>(pose_eng));

    // Offset for velocity and angular velocity conversion is zero as the RoadRunner
    // coordinate system is stationary in the ECEF frame (i.e., earth-fixed)
    transEng2Ecef[3].x = 0.0;
    transEng2Ecef[3].y = 0.0;
    transEng2Ecef[3].z = 0.0;

    auto transEcef2Eng = glm::inverse(transEng2Ecef);

    // Convert velocity
    glm::vec4 v_ecef(
        ecefPose.velocity().x(),
        ecefPose.velocity().y(),
        ecefPose.velocity().z(),
        1);
    auto v_eng = transEcef2Eng * v_ecef;
    engPose.mutable_velocity()->set_x(v_eng.x);
    engPose.mutable_velocity()->set_y(v_eng.y);
    engPose.mutable_velocity()->set_z(v_eng.z);

    // Convert angular velocity
    glm::vec4 av_ecef(
        ecefPose.angular_velocity().x(),
        ecefPose.angular_velocity().y(),
        ecefPose.angular_velocity().z(),
        1);
    auto av_eng = transEcef2Eng * av_ecef;
    engPose.mutable_angular_velocity()->set_x(av_eng.x);
    engPose.mutable_angular_velocity()->set_y(av_eng.y);
    engPose.mutable_angular_velocity()->set_z(av_eng.z);
}

////////////////////////////////////////////////////////////////////////////////

// void Util::ConvertPoseEcefToProj(
//     const sseProto::ActorPose& ecefPose,
//     sseProto::ActorPose&       projPose) {
//     // Step 1: Find the equivalent geodetic location for given ecef coordinate
//     // This is following the WGS84 datum
//
//     auto lla = mpProjectionUtil::GetECEF2LLA(ecefPose.pose().matrix().col3().x(), ecefPose.pose().matrix().col3().y(), ecefPose.pose().matrix().col3().z());
//
//     // Step 2: Get the cartesian coordinates corresponding to the lla. So lla -> [x,y]
//     // For this conversion, the projection string is required
//
//     auto destProjStr = projPose.coordinate_system_ref().coordinate_system().projected_coordinate_system().projection();
//
//     rrSP<const mpProjectionUtil::mpProjection> destProj;
//     destProj = mpProjectionUtil::CreateFromProjectionString(destProjStr);
//
//     rrSP<mpProjectionUtil::mpProjectionTransform> projTransform;
//
//     projTransform = std::make_shared<mpProjectionUtil::mpProjectionTransform>(*mpProjectionUtil::GetStandardLatLong(), *destProj);
//
//     glm::dvec3 pos(lla.x, lla.y, lla.z);
//
//     // Transform() will project the points on a 2D map and give the x,y coordinates (refer TransformScene implementation)
//     projTransform->Transform(pos);
//     // Convert pose
//
//     auto transEng2Ecef = mpProjectionUtil::GetLLA2ECEFTransform(lla.x, lla.y, lla.z);
//
//     auto new_pose = glm::inverse(transEng2Ecef) *
//                     rrConvert<glm::dmat4>(ecefPose.pose().matrix());
//
//     // Copy over projected coordinates
//     new_pose[3].x = pos.x;
//     new_pose[3].y = pos.y;
//     new_pose[3].z = pos.z;
//
//     projPose.mutable_pose()->mutable_matrix()->CopyFrom(
//         rrConvert<sseProtoCommon::Matrix4x4>(new_pose));
//
//     // Offset for velocity and angular velocity conversion is zero as the RoadRunner
//     // coordinate system is stationary in the ECEF frame (i.e., earth-fixed)
//     transEng2Ecef[3].x = 0.0;
//     transEng2Ecef[3].y = 0.0;
//     transEng2Ecef[3].z = 0.0;
//
//     auto transEcef2Eng = glm::inverse(transEng2Ecef);
//
//     // Convert velocity
//     glm::vec4 v_ecef(
//         ecefPose.velocity().x(),
//         ecefPose.velocity().y(),
//         ecefPose.velocity().z(),
//         1);
//     auto v_proj = transEcef2Eng * v_ecef;
//     projPose.mutable_velocity()->set_x(v_proj.x);
//     projPose.mutable_velocity()->set_y(v_proj.y);
//     projPose.mutable_velocity()->set_z(v_proj.z);
//
//     // Convert angular velocity
//     glm::vec4 av_ecef(
//         ecefPose.angular_velocity().x(),
//         ecefPose.angular_velocity().y(),
//         ecefPose.angular_velocity().z(),
//         1);
//     auto av_proj = transEcef2Eng * av_ecef;
//     projPose.mutable_angular_velocity()->set_x(av_proj.x);
//     projPose.mutable_angular_velocity()->set_y(av_proj.y);
//     projPose.mutable_angular_velocity()->set_z(av_proj.z);
// }

////////////////////////////////////////////////////////////////////////////////

void Util::ConvertPoseEngToEcef(
    const sseProto::ActorPose& engPose,
    sseProto::ActorPose&       ecefPose) {
    const auto& origin = engPose.coordinate_system_ref().coordinate_system().engineering_coordinate_system().geodetic_origin();

    auto transEng2Ecef = mpProjectionUtil::GetLLA2ECEFTransform(origin.latitude(),
                                                                origin.longitude(), origin.altitude());

    // Convert pose
    auto pose_ecef = transEng2Ecef *
                     rrConvert<glm::dmat4>(engPose.pose().matrix());
    ecefPose.mutable_pose()->mutable_matrix()->CopyFrom(
        rrConvert<sseProtoCommon::Matrix4x4>(pose_ecef));

    // Offset for velocity and angular velocity conversion is zero as the RoadRunner
    // coordinate system is stationary in the ECEF frame (i.e., earth-fixed)
    transEng2Ecef[3].x = 0.0;
    transEng2Ecef[3].y = 0.0;
    transEng2Ecef[3].z = 0.0;

    // Convert velocity
    glm::vec4 v_eng(
        engPose.velocity().x(),
        engPose.velocity().y(),
        engPose.velocity().z(),
        1);
    auto v_ecef = transEng2Ecef * v_eng;
    ecefPose.mutable_velocity()->set_x(v_ecef.x);
    ecefPose.mutable_velocity()->set_y(v_ecef.y);
    ecefPose.mutable_velocity()->set_z(v_ecef.z);

    // Convert angular velocity
    glm::vec4 av_eng(
        engPose.angular_velocity().x(),
        engPose.angular_velocity().y(),
        engPose.angular_velocity().z(),
        1);
    auto av_ecef = transEng2Ecef * av_eng;
    ecefPose.mutable_angular_velocity()->set_x(av_ecef.x);
    ecefPose.mutable_angular_velocity()->set_y(av_ecef.y);
    ecefPose.mutable_angular_velocity()->set_z(av_ecef.z);
}

////////////////////////////////////////////////////////////////////////////////

void Util::ConvertPoseEngToEng(
    const sseProto::ActorPose& eng1Pose,
    sseProto::ActorPose&       eng2Pose) {
    // If two engineering coordinate systems are same, directly copy the pose over
    if (eng1Pose.coordinate_system_ref().coordinate_system().engineering_coordinate_system() ==
        eng2Pose.coordinate_system_ref().coordinate_system().engineering_coordinate_system()) {
        eng2Pose.mutable_pose()->CopyFrom(eng1Pose.pose());
        eng2Pose.mutable_velocity()->CopyFrom(eng1Pose.velocity());
        eng2Pose.mutable_angular_velocity()->CopyFrom(eng1Pose.angular_velocity());
        return;
    }

    // Convert the source pose to geocentric pose
    sseProto::ActorPose ecefPose;
    ecefPose.set_reference_frame(sseProto::ReferenceFrame::REFERENCE_FRAME_SPECIFIED);
    ecefPose.mutable_coordinate_system_ref()->mutable_coordinate_system()->mutable_geocentric_coordinate_system();
    ConvertPoseEngToEcef(eng1Pose, ecefPose);

    // Convert the geocentric pose to the destination engineering coordinate system
    ConvertPoseEcefToEng(ecefPose, eng2Pose);
}

////////////////////////////////////////////////////////////////////////////////

// void Util::ConvertPoseEngToProj(
//     const sseProto::ActorPose& engPose,
//     sseProto::ActorPose&       projPose) {
//     // Convert the source pose to geocentric pose
//     sseProto::ActorPose ecefPose;
//     ecefPose.set_reference_frame(sseProto::ReferenceFrame::REFERENCE_FRAME_SPECIFIED);
//     ecefPose.mutable_coordinate_system_ref()->mutable_coordinate_system()->mutable_geocentric_coordinate_system();
//     ConvertPoseEngToEcef(engPose, ecefPose);
//
//     // Convert the geocentric pose to the destination engineering coordinate system
//     ConvertPoseEcefToProj(ecefPose, projPose);
// }

////////////////////////////////////////////////////////////////////////////////

glm::vec3 Util::ConvertCoordEcefToGeog(const glm::vec3 ecefCoord) {
    return mpProjectionUtil::GetECEF2LLA(ecefCoord.x, ecefCoord.y, ecefCoord.z);
}

////////////////////////////////////////////////////////////////////////////////

glm::vec3 Util::ConvertCoordEngToGeog(const glm::vec3 engCoord, const glm::vec3 origin) {
    auto transEng2Ecef = mpProjectionUtil::GetLLA2ECEFTransform(origin.x, origin.y, origin.z);

    glm::dmat4 poseMatrix;
    poseMatrix[3].x = engCoord.x;
    poseMatrix[3].y = engCoord.y;
    poseMatrix[3].z = engCoord.z;
    poseMatrix[3].w = 1.0;

    auto pose_ecef = transEng2Ecef * poseMatrix;

    glm::vec3 ecefCoord(pose_ecef[3].x, pose_ecef[3].y, pose_ecef[3].z);

    return ConvertCoordEcefToGeog(ecefCoord);
}

////////////////////////////////////////////////////////////////////////////////

// glm::vec3 Util::ConvertCoordProjToGeog(const glm::vec3 projCoord, const std::string& sourceProjStr) {
//     rrSP<const mpProjectionUtil::mpProjection> sourceProj;
//     sourceProj = mpProjectionUtil::CreateFromProjectionString(sourceProjStr);
//
//     rrSP<mpProjectionUtil::mpProjectionTransform> projTransform;
//
//     // We only support to/from conversions for latitudes and longitudes in WGS84 datum. Hence using GetStandardLatLong()
//     projTransform = std::make_shared<mpProjectionUtil::mpProjectionTransform>(*sourceProj, *mpProjectionUtil::GetStandardLatLong());
//
//     glm::dvec3 pos(projCoord.x, projCoord.y, projCoord.z);
//     projTransform->Transform(pos);
//
//     return pos;
// }

////////////////////////////////////////////////////////////////////////////////

// glm::vec3 Util::ConvertCoordGeogToProj(const glm::vec3 projCoord, const std::string& destProjStr) {
//     rrSP<const mpProjectionUtil::mpProjection> destProj;
//     destProj = mpProjectionUtil::CreateFromProjectionString(destProjStr);
//
//     rrSP<mpProjectionUtil::mpProjectionTransform> projTransform;
//
//     // We only support to/from conversions for latitudes and longitudes in WGS84 datum. Hence using GetStandardLatLong()
//     projTransform = std::make_shared<mpProjectionUtil::mpProjectionTransform>(*mpProjectionUtil::GetStandardLatLong(), *destProj);
//
//     glm::dvec3 pos(projCoord.x, projCoord.y, projCoord.z);
//     projTransform->Transform(pos);
//
//     return pos;
// }

////////////////////////////////////////////////////////////////////////////////

bool Util::ConvertActorPose(const sseProto::ActorPose& from, sseProto::ActorPose& to, std::string* outErrorString) {
    // Validate that coordinate systems before and after the conversion have been
    // explicitly specified. Any indirect coordinate system references (e.g.,
    // referencing the coordinate system defined by a world actor) should be
    // resolved into explicit specification before calling this function.
    if (from.reference_frame() != sseProto::ReferenceFrame::REFERENCE_FRAME_SPECIFIED ||
        !from.coordinate_system_ref().has_coordinate_system()) {
        if (outErrorString) {
            *outErrorString = "Invalid actor pose conversion: coordinate system for the source actor pose is unspecified.";
        }
        return false;
    }
    const auto& fromCoord = from.coordinate_system_ref().coordinate_system();

    if (to.reference_frame() != sseProto::ReferenceFrame::REFERENCE_FRAME_SPECIFIED ||
        !to.coordinate_system_ref().has_coordinate_system()) {
        if (outErrorString) {
            *outErrorString = "Invalid actor pose conversion: coordinate system for the destination actor pose is unspecified.";
        }
        return false;
    }
    const auto& toCoord = to.coordinate_system_ref().coordinate_system();

    // Check equality between source and destination coordinate systems
    if (fromCoord == toCoord) {
        // Copy over the pose because no conversion is needed
        to.mutable_pose()->CopyFrom(from.pose());
        to.mutable_velocity()->CopyFrom(from.velocity());
        to.mutable_angular_velocity()->CopyFrom(from.angular_velocity());
        return true;
    }

    // Do conversion
    switch (fromCoord.type_case()) {
    case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kProjectedCoordinateSystem:
        switch (toCoord.type_case()) {
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeographicCoordinateSystem:
            if (outErrorString) {
                *outErrorString = "Conversion from/to a projected coordinate system is not supported.";
            }
            return false;
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeocentricCoordinateSystem:
            if (outErrorString) {
                *outErrorString = "Conversion from/to a projected coordinate system is not supported.";
            }
            return false;
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kEngineeringCoordinateSystem:
            if (outErrorString) {
                *outErrorString = "Conversion from/to a projected coordinate system is not supported.";
            }
            return false;
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kProjectedCoordinateSystem:
        default:
            if (outErrorString) {
                *outErrorString = "Invalid actor pose conversion: incorrect destination coordinate system type.";
            }
            return false;
        }
    case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeographicCoordinateSystem:
        switch (toCoord.type_case()) {
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kProjectedCoordinateSystem:
            if (outErrorString) {
                *outErrorString = "Conversion from/to a projected coordinate system is not supported.";
            }
            return false;
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeocentricCoordinateSystem:
            if (outErrorString) {
                *outErrorString = "Conversion from/to a geographic coordinate system is not supported.";
            }
            return false;
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kEngineeringCoordinateSystem:
            if (outErrorString) {
                *outErrorString = "Conversion from/to a geographic coordinate system is not supported.";
            }
            return false;
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeographicCoordinateSystem:
        default:
            if (outErrorString) {
                *outErrorString = "Invalid actor pose conversion: incorrect destination coordinate system type.";
            }
            return false;
        }
    case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeocentricCoordinateSystem:
        switch (toCoord.type_case()) {
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kProjectedCoordinateSystem:
            if (outErrorString) {
                *outErrorString = "Conversion from/to a projected coordinate system is not supported.";
            }
            return false;
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeographicCoordinateSystem:
            if (outErrorString) {
                *outErrorString = "Conversion from/to a geographic coordinate system is not supported.";
            }
            return false;
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kEngineeringCoordinateSystem:
            ConvertPoseEcefToEng(from, to);
            return true;
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeocentricCoordinateSystem:
        default:
            if (outErrorString) {
                *outErrorString = "Invalid actor pose conversion: incorrect destination coordinate system type.";
            }
            return false;
        }
    case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kEngineeringCoordinateSystem:
        switch (toCoord.type_case()) {
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kProjectedCoordinateSystem:
            if (outErrorString) {
                *outErrorString = "Conversion from/to a projected coordinate system is not supported.";
            }
            return false;
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeographicCoordinateSystem:
            if (outErrorString) {
                *outErrorString = "Conversion from/to a geographic coordinate system is not supported.";
            }
            return false;
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeocentricCoordinateSystem:
            ConvertPoseEngToEcef(from, to);
            return true;
        case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kEngineeringCoordinateSystem:
            ConvertPoseEngToEng(from, to);
            return true;
        default:
            if (outErrorString) {
                *outErrorString = "Invalid actor pose conversion: incorrect destination coordinate system type.";
            }
            return false;
        }
    default:
        if (outErrorString) {
            *outErrorString = "Invalid actor pose conversion: incorrect source coordinate system type.";
        }
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace rrProtoGrpc

////////////////////////////////////////////////////////////////////////////////
