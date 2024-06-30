/* Copyright 2021-2023 The MathWorks, Inc. */

#include "mw_grpc_clients/roadrunner/RoadRunnerServiceClient.hpp"
#include "roadrunnerproto/mathworks/roadrunner/roadrunner_service.grpc.pb.h"
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

struct RoadRunnerGrpcContext {
    std::unique_ptr<RoadRunnerService::Stub> mStub;
    std::shared_ptr<grpc::ChannelInterface>  mChannel;
};

namespace {
std::unique_ptr<RoadRunnerGrpcContext> initSysApiBeforeConstructing() {
    mwgrpc::core::init(); // Ensure gRPC system is initialized
    return std::make_unique<RoadRunnerGrpcContext>();
}
} // namespace

RoadRunnerServiceClient::RoadRunnerServiceClient()
    : mGrpcContext(initSysApiBeforeConstructing())
    , mApiPort(cDefaultApiPort) {}

RoadRunnerServiceClient::~RoadRunnerServiceClient() = default;

void RoadRunnerServiceClient::Connect(int apiPort) {
    if (apiPort < cMinPort || apiPort > cMaxPort) {
        throw fl::except::MakeException(roadrunnerapi::roadrunnerservice::invalidApiPort(std::to_string(cMinPort), std::to_string(cMaxPort)));
    }
    mApiPort               = apiPort;
    mGrpcContext->mChannel = grpc::CreateChannel("localhost:" + std::to_string(apiPort),
                                                 grpc::InsecureChannelCredentials());
    mGrpcContext->mStub    = RoadRunnerService::NewStub(mGrpcContext->mChannel);
}

bool RoadRunnerServiceClient::IsServerReady() const {
    // Wait up to 5 seconds during each call to connect to the server.
    mGrpcContext->mChannel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::milliseconds(5000));
    return mGrpcContext->mChannel->GetState(true) == grpc_connectivity_state::GRPC_CHANNEL_READY;
}

GetVariableResponse RoadRunnerServiceClient::GetScenarioVariable(const GetVariableRequest* request) const {
    GetVariableResponse response;
    grpc::ClientContext context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->GetScenarioVariable(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
    return response;
}

RoadRunnerStatusResponse RoadRunnerServiceClient::RoadRunnerStatus(const RoadRunnerStatusRequest* request) const {
    RoadRunnerStatusResponse response;
    grpc::ClientContext      context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->RoadRunnerStatus(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
    return response;
}

GetAllVariablesResponse RoadRunnerServiceClient::GetAllScenarioVariables(const GetAllVariablesRequest* request) const {
    GetAllVariablesResponse response;
    grpc::ClientContext     context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->GetAllScenarioVariables(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
    return response;
}

ChangeWorldSettingsResponse RoadRunnerServiceClient::ChangeWorldSettings(const ChangeWorldSettingsRequest* request) const {
    ChangeWorldSettingsResponse response;
    grpc::ClientContext         context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->ChangeWorldSettings(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
    return response;
}

GetAnchorsResponse RoadRunnerServiceClient::GetAnchors(const GetAnchorsRequest* request) const {
    GetAnchorsResponse  response;
    grpc::ClientContext context;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    grpc::Status status = mGrpcContext->mStub->GetAnchors(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
    return response;
}

// Call a RoadRunnerService method
void RoadRunnerServiceClient::NewProject(const NewProjectRequest* request) const {
    NewProjectResponse  response;
    grpc::ClientContext context;
    grpc::Status        status;
    std::string         matlabMethodName = "newProject";
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->NewProject(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::LoadProject(const LoadProjectRequest* request) const {
    LoadProjectResponse response;
    grpc::ClientContext context;
    grpc::Status        status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->LoadProject(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::SaveProject(const SaveProjectRequest* request) const {
    SaveProjectResponse response;
    grpc::ClientContext context;
    grpc::Status        status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->SaveProject(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::NewScene(const NewSceneRequest* request) const {
    NewSceneResponse    response;
    grpc::ClientContext context;
    grpc::Status        status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->NewScene(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::LoadScene(const LoadSceneRequest* request) const {
    LoadSceneResponse   response;
    grpc::ClientContext context;
    grpc::Status        status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->LoadScene(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::SaveScene(const SaveSceneRequest* request) const {
    SaveSceneResponse   response;
    grpc::ClientContext context;
    grpc::Status        status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->SaveScene(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::Export(const ExportRequest* request) const {
    ExportResponse      response;
    grpc::ClientContext context;
    grpc::Status        status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->Export(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::Import(const ImportRequest* request) const {
    ImportResponse      response;
    grpc::ClientContext context;
    grpc::Status        status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->Import(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::Exit(const ExitRequest* request) const {
    ExitResponse        response;
    grpc::ClientContext context;
    grpc::Status        status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->Exit(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::NewScenario(const NewScenarioRequest* request) const {
    NewScenarioResponse response;
    grpc::ClientContext context;
    grpc::Status        status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->NewScenario(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::LoadScenario(const LoadScenarioRequest* request) const {
    LoadScenarioResponse response;
    grpc::ClientContext  context;
    grpc::Status         status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->LoadScenario(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::SaveScenario(const SaveScenarioRequest* request) const {
    SaveScenarioResponse response;
    grpc::ClientContext  context;
    grpc::Status         status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->SaveScenario(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::SetScenarioVariable(const SetVariableRequest* request) const {
    SetVariableResponse response;
    grpc::ClientContext context;
    grpc::Status        status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->SetScenarioVariable(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::PrepareSimulation(const PrepareSimulationRequest* request) const {
    PrepareSimulationResponse response;
    grpc::ClientContext       context;
    grpc::Status              status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->PrepareSimulation(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::SimulateScenario(const SimulateScenarioRequest* request) const {
    SimulateScenarioResponse response;
    grpc::ClientContext      context;
    grpc::Status             status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->SimulateScenario(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}

void RoadRunnerServiceClient::RemapAnchor(const RemapAnchorRequest* request) const {
    RemapAnchorResponse response;
    grpc::ClientContext context;
    grpc::Status        status;
    FOUNDATION_LOG_FUNCTION(logger_);
    FOUNDATION_LOG_INFO(logger_, << "Starting the gRPC call.");
    status = mGrpcContext->mStub->RemapAnchor(&context, *request, &response);
    if (!status.ok()) {
        Util::ReportGrpcError(status);
    } else {
        FOUNDATION_LOG_INFO(logger_, << "Finished the gRPC call successfully.");
    }
}


} // namespace roadrunnerapi
