/* Copyright 2021-2023 The MathWorks, Inc. */

#include "mw_grpc_clients/roadrunner/RoadRunnerExperimental.hpp"
#include "roadrunnerproto/mathworks/roadrunner/scene_api_experimental.grpc.pb.h"
#include "Util.hpp"

#include <fl/except/MsgIDException.hpp>
#include "resources/roadrunnerapi/roadrunnerservice.hpp"
#include "mwgrpc/core/mwgrpc.hpp"

#include <grpcpp/grpcpp.h>
#include <chrono>

using namespace mathworks::roadrunner;
using grpc::ClientContext;
using grpc::Status;

namespace roadrunnerapi {

struct RoadRunnerExperimentalGrpcContext {
    std::unique_ptr<RoadRunnerSceneApiExperimental::Stub> mStub;
    std::shared_ptr<grpc::ChannelInterface>               mChannel;
};

namespace {
std::unique_ptr<RoadRunnerExperimentalGrpcContext> initSysApiBeforeConstructing() {
    mwgrpc::core::init(); // Ensure gRPC system is initialized
    return std::make_unique<RoadRunnerExperimentalGrpcContext>();
}
} // namespace

RoadRunnerExperimental::RoadRunnerExperimental()
    : mGrpcContext(initSysApiBeforeConstructing())
    , mApiPort(cDefaultApiPort) {}

RoadRunnerExperimental::~RoadRunnerExperimental() = default;

void RoadRunnerExperimental::Connect(int apiPort) {
    if (apiPort < cMinPort || apiPort > cMaxPort) {
        throw fl::except::MakeException(roadrunnerapi::roadrunnerservice::invalidApiPort(
            std::to_string(cMinPort), std::to_string(cMaxPort)));
    }
    mApiPort = apiPort;
    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(25 * 1024 * 1024);
    mGrpcContext->mChannel = grpc::CreateCustomChannel("localhost:" + std::to_string(apiPort),
                                                       grpc::InsecureChannelCredentials(), args);
    mGrpcContext->mStub    = RoadRunnerSceneApiExperimental::NewStub(mGrpcContext->mChannel);
}

bool RoadRunnerExperimental::IsServerReady() const {
    // Wait up to 5 seconds during each call to connect to the server.
    mGrpcContext->mChannel->WaitForConnected(std::chrono::system_clock::now() +
                                             std::chrono::milliseconds(5000));
    return mGrpcContext->mChannel->GetState(true) == grpc_connectivity_state::GRPC_CHANNEL_READY;
}

GetVariableResponse RoadRunnerExperimental::GetSceneVariable(const GetVariableRequest* request) const {
    GetVariableResponse response;
    grpc::ClientContext context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->GetSceneVariable(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
    return response;
}

CreateRoadResponse RoadRunnerExperimental::CreateRoad(const CreateRoadRequest* request) const {
    CreateRoadResponse  response;
    grpc::ClientContext context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->CreateRoad(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
    return response;
}

GetActiveLanesAtDistanceResponse RoadRunnerExperimental::GetActiveLanesAtDistance(
    const GetActiveLanesAtDistanceRequest* request) const {
    GetActiveLanesAtDistanceResponse response;
    grpc::ClientContext              context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status =
        mGrpcContext->mStub->GetActiveLanesAtDistance(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
    return response;
}

void RoadRunnerExperimental::SetSceneVariable(const SetVariableRequest* request) const {
    SetVariableResponse response;
    grpc::ClientContext context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status;
    status = mGrpcContext->mStub->SetSceneVariable(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerExperimental::StartBatchCommands(const StartBatchCommandsRequest* request) const {
    StartBatchCommandsResponse response;
    grpc::ClientContext        context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status;
    status = mGrpcContext->mStub->StartBatchCommands(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerExperimental::EndBatchCommands(const EndBatchCommandsRequest* request) const {
    EndBatchCommandsResponse response;
    grpc::ClientContext      context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status;
    status = mGrpcContext->mStub->EndBatchCommands(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

std::string RoadRunnerExperimental::GetMesh(const GetMeshRequest* request) const {
    GetMeshResponse     response;
    grpc::ClientContext context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");

    auto        reader = mGrpcContext->mStub->GetMesh(&context, *request);
    std::string responseStr;

    while (reader->Read(&response)) {
        responseStr.append(response.mesh());
    }

    grpc::Status status = reader->Finish();
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }

    return responseStr;
}

CreateCommandMarkerResponse RoadRunnerExperimental::CreateCommandMarker() const {
    CreateCommandMarkerRequest  request;
    CreateCommandMarkerResponse response;
    grpc::ClientContext         context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->CreateCommandMarker(&context, request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
    return response;
}

void RoadRunnerExperimental::Undo(
    const UndoRequest* request) const {
    UndoResponse        response;
    grpc::ClientContext context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->Undo(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerExperimental::Redo(const RedoRequest* request) const {
    RedoResponse        response;
    grpc::ClientContext context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->Redo(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

CreateOutputMessageMarkerResponse RoadRunnerExperimental::CreateOutputMessageMarker() const {
    CreateOutputMessageMarkerRequest  request;
    CreateOutputMessageMarkerResponse response;
    grpc::ClientContext               context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->CreateOutputMessageMarker(&context, request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
    return response;
}

GetOutputMessagesResponse RoadRunnerExperimental::GetOutputMessages(const GetOutputMessagesRequest* request) const {
    GetOutputMessagesResponse response;
    grpc::ClientContext       context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->GetOutputMessages(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
    return response;
}

void RoadRunnerExperimental::AddOutputMessages(const AddOutputMessagesRequest* request) const {
    AddOutputMessagesResponse response;
    grpc::ClientContext       context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->AddOutputMessages(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

} // namespace roadrunnerapi
