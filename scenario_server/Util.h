#pragma once

#include <google/protobuf/message.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server.h>
#include <grpcpp/client_context.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "RoadRunnerConversions.h"

#include <future>
#include <string>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/common.hpp>
#include <glm/mat4x4.hpp>

////////////////////////////////////////////////////////////////////////////////

namespace rrProtoGrpc {
static const int cMinPort{1024};
static const int cMaxPort{65535};

// Value chosen higher than gRPC's default of 4 Mb intended to allow simulation on
// imported HD Map scenes of reasonable size (~ 1km x 1km dense).
//
// Values higher than this may still work (upper limit has not been explored).
//
// Note - Must be manually synchronized with Tools/CARLA/SimulationClient/SimulationClient.py
static const int cMaxMessageSize{25 * 1024 * 1024};

class Util {
  public:
    // Throws exception on failure.
    // - Server is ready to handle requests once this function has returned.
    // - Most callers will want to hold on to the QFuture to verify that the server has shutdown.
    //   e.g. call server->Shutdown() and then future.waitForFinished() to verify the server has
    //   shutdown.

    template <typename ServiceT>
    static std::future<void> StartServer(rrUP<grpc::Server>& outServer, grpc::Service* service, const std::string& address);

    // Start a server with an existing builder
    // - Throws on failure
    // - address is only used for error message construction

    static std::future<void> StartServer(rrUP<grpc::Server>& outServer, grpc::ServerBuilder& builder, const std::string& address);

    // Create a ServerBuilder with our default settings

    template <typename ServiceT>
    static rrUP<grpc::ServerBuilder> MakeBuilder(grpc::Service* service, const std::string& address);

    // Throws exceptions on failure.

    template <typename ServiceT, typename StubT>
    static rrUP<StubT> CreateStub(const std::string& address);

    /*static double GetDefaultRPCTimeoutSeconds();
    static std::chrono::milliseconds GetDefaultRPCTimeout();
    static std::chrono::time_point<std::chrono::system_clock> GetDefaultRPCDeadline();*/

    // Initializes a client context with the default timeout value and optionally sets the clientID (used in logging)
    /*static void InitContext(grpc::ClientContext & inoutContext, const std::string & clientID = std::string("UnknownClient"));*/

    // Throws an exception with a suitable message if the given status is not 'ok'
    //	- If the status includes an error message, it will be included in the exception message text
    //		- Otherwise, default messages will be supplied for common-case codes
    /*static void CheckResult(const grpc::Status& status);*/

    //? TODO: Remove these in favor of calls to CreateLocalHostAddress
    /*static std::string GetCoSimServerAddress();*/

    // Builds an address of the form localhost:<port>
    /*static QString CreateLocalHostAddress(int port);*/

    // Builds an address of the form <hostname>:<port>
    static std::string CreateAddress(const std::string& hostname, int port);

    // Returns a restriction object for the valid port range [cMinPort, cMaxPort]
    /*static roadrunner::core::IntRestriction PortRangeRestriction();*/

    // TODO: Move to proto::ReflectionUtil
    //
    // Retrieves the oneof message corresponding to the given OneofDescriptor in parentMessage.
    // - Throws if no oneof field is set.
    // - If expectedCase is set, throws if the oneof field case isn't equal to expectedCase.
    //  - Tip: oneofDescriptor can be retrieved from a field name using
    //    "ParentMessage.GetDescriptor()->FindOneofByName("my_oneof_field_name");"
    static const google::protobuf::Message& GetOneofMessage(
        const google::protobuf::Message&         parentMessage,
        const google::protobuf::OneofDescriptor& oneofDescriptor,
        rrOpt<int>                               expectedCase = std::nullopt);

    static void ConvertPoseEcefToEng(
        const sseProto::ActorPose& ecefPose,
        sseProto::ActorPose&       engPose);

    // static void ConvertPoseEcefToProj(
    //     const sseProto::ActorPose& ecefPose,
    //     sseProto::ActorPose&       projPose);

    static void ConvertPoseEngToEcef(
        const sseProto::ActorPose& engPose,
        sseProto::ActorPose&       ecefPose);

    static void ConvertPoseEngToEng(
        const sseProto::ActorPose& eng1Pose,
        sseProto::ActorPose&       eng2Pose);

    // static void ConvertPoseEngToProj(
    //     const sseProto::ActorPose& engPose,
    //     sseProto::ActorPose&       projPose);

    static glm::vec3 ConvertCoordEngToGeog(const glm::vec3 engCoord, const glm::vec3 origin);

    static glm::vec3 ConvertCoordEcefToGeog(const glm::vec3 ecefCoord);

    // static glm::vec3 ConvertCoordProjToGeog(const glm::vec3 projCoord, const std::string& sourceProjStr);

    // static glm::vec3 ConvertCoordGeogToProj(const glm::vec3 projCoord, const std::string& destProjStr);

    static bool ConvertActorPose(const sseProto::ActorPose& from, sseProto::ActorPose& to, std::string* outErrorString);
};

////////////////////////////////////////////////////////////////////////////////

template <typename ServiceT>
std::future<void> Util::StartServer(rrUP<grpc::Server>& outServer, grpc::Service* service, const std::string& address) {
    auto builder = MakeBuilder<ServiceT>(service, address);
    return StartServer(outServer, *builder, address);
}

////////////////////////////////////////////////////////////////////////////////

template <typename ServiceT>
rrUP<grpc::ServerBuilder> Util::MakeBuilder(grpc::Service* service, const std::string& address) {
    auto builder = std::make_unique<grpc::ServerBuilder>();

    builder->AddListeningPort(address, grpc::InsecureServerCredentials());
    // Increase default number of pollers to support additional clients.
    // - Profiling indicates that this is necessary but the root causes are not well understood.
    builder->SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MAX_POLLERS, 20);
    builder->RegisterService((ServiceT*)service);
    builder->SetMaxSendMessageSize(rrProtoGrpc::cMaxMessageSize);
    builder->SetMaxReceiveMessageSize(rrProtoGrpc::cMaxMessageSize);

    return builder;
}

////////////////////////////////////////////////////////////////////////////////

template <typename ServiceT, typename StubT>
rrUP<StubT> Util::CreateStub(const std::string& address) {
    grpc::ChannelArguments args;

    args.SetMaxSendMessageSize(rrProtoGrpc::cMaxMessageSize);
    args.SetMaxReceiveMessageSize(rrProtoGrpc::cMaxMessageSize);

    auto channel = grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), args);
    // Disabled for now as we apply this timeout for each rpc call we make so this one is redundant.
    // channel->WaitForConnected(GetDefaultRPCDeadline());

    // Disabled for now as this failure does not capture any meaningful error state, rather
    // rpc callers should check the status of their rpc responses and the corresponding error messages
    //
    // This is particularly important for the first call made after creating the stub as creating the
    // stub does not ensure that a connection has been established.
    // rrThrowVerify(channel->GetState(true) == grpc_connectivity_state::GRPC_CHANNEL_READY, "gRPC Channel was unable to reach ready state while attempting to connect to %1."_Q % address);

    return ServiceT::NewStub(channel);
}

} // namespace rrProtoGrpc

////////////////////////////////////////////////////////////////////////////////
