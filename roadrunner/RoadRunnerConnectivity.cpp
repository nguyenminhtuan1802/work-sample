/* Copyright 2021-2023 The MathWorks, Inc. */

#include "mw_grpc_clients/roadrunner/RoadRunnerConnectivity.hpp"
#include "roadrunnerproto/mathworks/roadrunner/network_connectivity_api.grpc.pb.h"
#include "Util.hpp"
#include "mwgrpc/core/mwgrpc.hpp"

#include <fl/except/MsgIDException.hpp>
#include "resources/roadrunnerapi/roadrunnerservice.hpp"

#include <grpcpp/grpcpp.h>
#include <chrono>

using namespace mathworks::roadrunner;
using grpc::ClientContext;
using grpc::Status;

namespace roadrunnerapi {

struct RoadRunnerConnectivityGrpcContext {
    std::unique_ptr<RoadRunnerNetworkConnectivityApi::Stub> mStub;
    std::shared_ptr<grpc::ChannelInterface>                 mChannel;
};

namespace {
std::unique_ptr<RoadRunnerConnectivityGrpcContext> initSysApiBeforeConstructing() {
    mwgrpc::core::init(); // Ensure gRPC system is initialized
    return std::make_unique<RoadRunnerConnectivityGrpcContext>();
}
} // namespace

RoadRunnerConnectivity::RoadRunnerConnectivity()
    : mGrpcContext(initSysApiBeforeConstructing())
    , mTestConnectionPort(cDefaultTestConnectionPort) {}

RoadRunnerConnectivity::~RoadRunnerConnectivity() = default;

void RoadRunnerConnectivity::Connect(int testPort) {
    if (testPort < cMinPort || testPort > cMaxPort) {
        throw fl::except::MakeException(roadrunnerapi::roadrunnerservice::invalidTestConnectionPort(
            std::to_string(cMinPort), std::to_string(cMaxPort)));
    }
    mTestConnectionPort    = testPort;
    mGrpcContext->mChannel = grpc::CreateChannel("localhost:" + std::to_string(testPort),
                                                 grpc::InsecureChannelCredentials());
    mGrpcContext->mStub    = RoadRunnerNetworkConnectivityApi::NewStub(mGrpcContext->mChannel);
}

bool RoadRunnerConnectivity::IsServerReady() const {
    // Wait up to 5 seconds during each call to connect to the server.
    mGrpcContext->mChannel->WaitForConnected(std::chrono::system_clock::now() +
                                             std::chrono::milliseconds(5000));
    return mGrpcContext->mChannel->GetState(true) == grpc_connectivity_state::GRPC_CHANNEL_READY;
}

void RoadRunnerConnectivity::TestConnection(const TestConnectionRequest* request) const {
    TestConnectionResponse response;
    grpc::ClientContext    context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status;
    status = mGrpcContext->mStub->TestConnection(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}


} // namespace roadrunnerapi
