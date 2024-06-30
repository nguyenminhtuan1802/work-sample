/* Copyright 2021-2023 The MathWorks, Inc. */
#include "mw_grpc_clients/scenario/ScenarioGrpcClient.hpp"

#include <fl/except/MsgIDException.hpp>
#include "foundation/feature/Feature.hpp"
#include "foundation/release_info/ReleaseInfo.hpp"
#include "matrix/error_warning.hpp"
#include "mwgrpc/core/mwgrpc.hpp"

#include <grpcpp/create_channel.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server.h>
#include <grpcpp/client_context.h>
#include "resources/ssm/scenariosimulation.hpp"

#include "mathworks/scenario/common/core.pb.h"
#include "mathworks/scenario/common/core.grpc.pb.h"
#include "mathworks/scenario/common/geometry.pb.h"
#include "mathworks/scenario/common/geometry.grpc.pb.h"

#include "mathworks/scenario/simulation/actor.pb.h"
#include "mathworks/scenario/simulation/actor.grpc.pb.h"
#include "mathworks/scenario/simulation/attributes.pb.h"
#include "mathworks/scenario/simulation/attributes.grpc.pb.h"
#include "mathworks/scenario/simulation/behavior.pb.h"
#include "mathworks/scenario/simulation/behavior.grpc.pb.h"
#include "mathworks/scenario/simulation/comparison.pb.h"
#include "mathworks/scenario/simulation/comparison.grpc.pb.h"
#include "mathworks/scenario/simulation/condition.pb.h"
#include "mathworks/scenario/simulation/condition.grpc.pb.h"
#include "mathworks/scenario/simulation/cosim_api.pb.h"
#include "mathworks/scenario/simulation/cosim_api.grpc.pb.h"
#include "mathworks/scenario/simulation/cosim.pb.h"
#include "mathworks/scenario/simulation/cosim.grpc.pb.h"
#include "mathworks/scenario/simulation/event.pb.h"
#include "mathworks/scenario/simulation/event.grpc.pb.h"
#include "mathworks/scenario/simulation/action.pb.h"
#include "mathworks/scenario/simulation/action.grpc.pb.h"
#include "mathworks/scenario/simulation/scenario.pb.h"
#include "mathworks/scenario/simulation/scenario.grpc.pb.h"
#include "mathworks/scenario/simulation/transition_dynamics.pb.h"
#include "mathworks/scenario/simulation/transition_dynamics.grpc.pb.h"

namespace {
namespace rrProtoGrpc {
namespace Util {
void InitContext(grpc::ClientContext& inoutContext) {
    inoutContext.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(1000));
}
} // namespace Util

static const int cMaxMessageSize{25 * 1024 * 1024};

}
} // namespace ::rrProtoGrpc

namespace scenariogrpc {

using namespace mathworks::scenario::simulation;

struct CosimGrpcContext {
    std::unique_ptr<CoSimulationApi::Stub> mStub;
    std::unique_ptr<grpc::ClientContext>   mClientContext;
    // The client reader to read events published by the server
    std::unique_ptr<grpc::ClientReader<Event>> mReader;
};

namespace {
std::unique_ptr<CosimGrpcContext> initSysApiBeforeConstructing() {
    mwgrpc::core::init(); // Ensure gRPC system is initialized
    return std::make_unique<CosimGrpcContext>();
}
} // namespace

ScenarioGrpcClient::ScenarioGrpcClient()
    : mGrpcContext(initSysApiBeforeConstructing())
    , mId()
    , mClientTimeout(300)
    , mIsShutDown(true) {}

ScenarioGrpcClient::~ScenarioGrpcClient() = default;

void ScenarioGrpcClient::subscribeEvents(bool synchronous, bool simulator, double timeout) {
    mGrpcContext->mClientContext = std::make_unique<grpc::ClientContext>();
    mClientTimeout               = timeout;
    // Get the reader context to read the events published by the server
    SubscribeEventsRequest request;
    request.set_client_id(mId);
    request.mutable_client_profile()->mutable_simulink_platform();
    request.mutable_client_profile()->set_synchronous(synchronous);
    request.mutable_client_profile()->set_simulate_actors(simulator);
    request.mutable_client_profile()->set_timeout_milliseconds(mClientTimeout * 1000);
    mGrpcContext->mReader = mGrpcContext->mStub->SubscribeEvents(mGrpcContext->mClientContext.get(), request);
    if (!mGrpcContext->mReader) {
        return;
    }

    // the reader is up and running
    mIsShutDown = false;
}

bool ScenarioGrpcClient::readEvent(mathworks::scenario::simulation::Event* event) {
    return mGrpcContext->mReader->Read(event);
}

bool ScenarioGrpcClient::isShutDown() const {
    return mIsShutDown.load();
}

RegisterClientResponse ScenarioGrpcClient::connect(const std::string& address,
                                                   const std::string& proposedClientId) {
    grpc::ChannelArguments args;
    args.SetMaxSendMessageSize(rrProtoGrpc::cMaxMessageSize);
    args.SetMaxReceiveMessageSize(rrProtoGrpc::cMaxMessageSize);
    mGrpcContext->mStub = CoSimulationApi::NewStub(
        grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), args));

    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);
    RegisterClientRequest  request;
    RegisterClientResponse response;

    // Set a name for the client
    request.set_name("MATLAB_Client");
    if (!proposedClientId.empty()) {
        // Using proposed client id; usually used by bootstrapping
        request.set_proposed_client_id(proposedClientId);
    }

    request.set_client_version(foundation::release_info::ReleaseInfo().getReleaseFamily());

    auto status = mGrpcContext->mStub->RegisterClient(&context, request, &response);
    mId         = response.client_id();

    foundation::feature::PerProcessFeature SSDVersionPackage("SSDVersion");
    if (SSDVersionPackage.get() > 0 && response.has_version_check_result() &&
        !response.version_check_result().is_version_supported()) {
        mxWarningMsgId(
            ssm::scenariosimulation::UnsupportedClientVersion(response.version_check_result()
                                                                  .diagnostic_message(),
                                                              response.version_check_result().server_version()));
    }

    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::UnableToConnectToScenarioServer(
            status.error_message(), address));
    }

    return response;
}

void ScenarioGrpcClient::shutdown() {
    mGrpcContext->mClientContext->TryCancel();
    mIsShutDown = true;
}

double ScenarioGrpcClient::getTimeout() const {
    return mClientTimeout;
}

bool ScenarioGrpcClient::uploadMap(UploadMapRequest& upMapReq) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    UploadMapResponse response;
    auto              status       = mGrpcContext->mStub->UploadMap(&context, upMapReq, &response);
    bool              uploadStatus = status.ok();
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return uploadStatus;
}

DownloadMapResponse ScenarioGrpcClient::downloadMap() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    DownloadMapRequest  downloadRequest;
    DownloadMapResponse response;
    auto                status = mGrpcContext->mStub->DownloadMap(&context, downloadRequest, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }

    return response;
}

UploadScenarioResponse ScenarioGrpcClient::uploadScenario(UploadScenarioRequest& upLoadReq,
                                                          bool&                  uploadStatus) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    UploadScenarioResponse response;
    auto                   status = mGrpcContext->mStub->UploadScenario(&context, upLoadReq, &response);
    uploadStatus                  = status.ok();
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

DownloadScenarioResponse ScenarioGrpcClient::downloadScenario() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    DownloadScenarioRequest  downloadRequest;
    DownloadScenarioResponse response;
    auto                     status = mGrpcContext->mStub->DownloadScenario(&context, downloadRequest, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

SetRuntimeActorsResponse ScenarioGrpcClient::setRuntimeActors(SetRuntimeActorsRequest& actorsReq) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    SetRuntimeActorsResponse response;
    auto                     status = mGrpcContext->mStub->SetRuntimeActors(&context, actorsReq, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

GetActorsResponse ScenarioGrpcClient::getActors() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    GetActorsRequest  actorsRequest;
    GetActorsResponse response;
    auto              status = mGrpcContext->mStub->GetActors(&context, actorsRequest, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

void ScenarioGrpcClient::notifyActionCompleteEvents(
    const google::protobuf::RepeatedPtrField<NotifyActionCompleteRequest>& events) {
    for (int i = 0; i < events.size(); ++i) {
        grpc::ClientContext context;
        rrProtoGrpc::Util::InitContext(context);
        NotifyActionCompleteResponse response;
        mGrpcContext->mStub->NotifyActionComplete(&context, events[i], &response);
    }
}

void ScenarioGrpcClient::sendEvents(const SendEventsRequest& events) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);
    SendEventsResponse response;
    auto               status = mGrpcContext->mStub->SendEvents(&context, events, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
}

ReceiveEventsResponse ScenarioGrpcClient::receiveEvents() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    ReceiveEventsResponse response;
    ReceiveEventsRequest  request;
    auto                  status = mGrpcContext->mStub->ReceiveEvents(&context, request, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }

    return response;
}

AddDiagnosticMessageResponse ScenarioGrpcClient::addDiagnosticMessage(
    AddDiagnosticMessageRequest& messageReq) {

    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    AddDiagnosticMessageResponse response;
    auto                         status = mGrpcContext->mStub->AddDiagnosticMessage(&context, messageReq, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

GetSimulationSettingsResponse ScenarioGrpcClient::getSimulationSettings() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    GetSimulationSettingsRequest  request;
    GetSimulationSettingsResponse response;
    auto                          status = mGrpcContext->mStub->GetSimulationSettings(&context, request, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

GetSimulationTimeResponse ScenarioGrpcClient::getSimulationTime() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    GetSimulationTimeRequest  request;
    GetSimulationTimeResponse response;
    auto                      status = mGrpcContext->mStub->GetSimulationTime(&context, request, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

StartSimulationResponse ScenarioGrpcClient::startSimulation(StartSimulationRequest& request) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    StartSimulationResponse response;
    auto                    status = mGrpcContext->mStub->StartSimulation(&context, request, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }

    return response;
}

RestartSimulationResponse ScenarioGrpcClient::restartSimulation(RestartSimulationRequest& request) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    RestartSimulationResponse response;
    auto                      status = mGrpcContext->mStub->RestartSimulation(&context, request, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

ToggleSimulationPausedResponse ScenarioGrpcClient::pauseSimulation(
    ToggleSimulationPausedRequest& request) {

    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    ToggleSimulationPausedResponse response;
    auto                           status = mGrpcContext->mStub->ToggleSimulationPaused(&context, request, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

StopSimulationResponse ScenarioGrpcClient::stopSimulation(StopSimulationRequest& request) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    StopSimulationResponse response;
    auto                   status = mGrpcContext->mStub->StopSimulation(&context, request, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

StopSimulationResponse ScenarioGrpcClient::stopSimulationRequested(StopSimulationRequest& request) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    StopSimulationResponse response;
    auto                   status = mGrpcContext->mStub->StopSimulationRequested(&context, request, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

StepSimulationResponse ScenarioGrpcClient::stepSimulation(StepSimulationRequest& request) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    StepSimulationResponse response;
    auto                   status = mGrpcContext->mStub->StepSimulation(&context, request, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

SetSimulationSettingsResponse ScenarioGrpcClient::setSimulationSettings(
    SetSimulationSettingsRequest& request) {

    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    SetSimulationSettingsResponse response;
    auto                          status = mGrpcContext->mStub->SetSimulationSettings(&context, request, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

SetSimulationPaceResponse ScenarioGrpcClient::setSimulationPace(SetSimulationPaceRequest& request) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    SetSimulationPaceResponse response;
    auto                      status = mGrpcContext->mStub->SetSimulationPace(&context, request, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

GetSimulationStatusResponse ScenarioGrpcClient::getSimulationStatus(
    GetSimulationStatusRequest& request) {

    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    GetSimulationStatusResponse response;
    auto                        status = mGrpcContext->mStub->GetSimulationStatus(&context, request, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

SetReadyResponse ScenarioGrpcClient::setClientReady() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);
    SetReadyRequest readyRequest;
    *readyRequest.mutable_client_id() = mId;
    SetReadyResponse response;
    auto             status = mGrpcContext->mStub->SetReady(&context, readyRequest, &response);

    return response;
}

QueryDiagnosticMessageLogResponse ScenarioGrpcClient::QueryDiagnosticMessageLog() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    QueryDiagnosticMessageLogRequest  req;
    QueryDiagnosticMessageLogResponse response;
    auto                              status = mGrpcContext->mStub->QueryDiagnosticMessageLog(&context, req, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

QueryWorldRuntimeLogResponse ScenarioGrpcClient::QueryWorldRuntimeLog() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    QueryWorldRuntimeLogRequest  req;
    QueryWorldRuntimeLogResponse response;
    auto                         status = mGrpcContext->mStub->QueryWorldRuntimeLog(&context, req, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

QueryDiagnosticMessageLogResponse ScenarioGrpcClient::QueryDiagnosticLog() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    QueryDiagnosticMessageLogRequest  req;
    QueryDiagnosticMessageLogResponse response;
    auto                              status = mGrpcContext->mStub->QueryDiagnosticMessageLog(&context, req, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

bool ScenarioGrpcClient::EnableWorldRuntimeLogging(bool isOn) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    EnableWorldRuntimeLoggingRequest req;
    req.set_log_world_runtime(isOn);
    EnableWorldRuntimeLoggingResponse response;
    auto                              status = mGrpcContext->mStub->EnableWorldRuntimeLogging(&context, req, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return true;
}

GetWorldRuntimeLoggingStatusResponse ScenarioGrpcClient::GetWorldRuntimeLoggingStatus() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    GetWorldRuntimeLoggingStatusRequest  req;
    GetWorldRuntimeLoggingStatusResponse response;
    auto                                 status = mGrpcContext->mStub->GetWorldRuntimeLoggingStatus(&context, req, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

bool ScenarioGrpcClient::EnableScenarioCoverageLogging(bool isOn) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    EnableScenarioCoverageLoggingRequest req;
    req.set_log_scenario_coverage(isOn);
    EnableScenarioCoverageLoggingResponse response;
    auto                                  status = mGrpcContext->mStub->EnableScenarioCoverageLogging(&context, req, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return true;
}

GetScenarioCoverageLoggingStatusResponse ScenarioGrpcClient::GetScenarioCoverageLoggingStatus() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    GetScenarioCoverageLoggingStatusRequest  req;
    GetScenarioCoverageLoggingStatusResponse response;
    auto                                     status = mGrpcContext->mStub->GetScenarioCoverageLoggingStatus(&context, req, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

QueryScenarioCoverageLogResponse ScenarioGrpcClient::QueryScenarioCoverageLog() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    QueryScenarioCoverageLogRequest  req;
    QueryScenarioCoverageLogResponse response;
    auto                             status = mGrpcContext->mStub->QueryScenarioCoverageLog(&context, req, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

GetPhaseStatusResponse ScenarioGrpcClient::GetPhaseStatus() {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    GetPhaseStatusRequest  req;
    GetPhaseStatusResponse res;
    auto                   status = mGrpcContext->mStub->GetPhaseStatus(&context, req, &res);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return res;
}

AddActorResponse ScenarioGrpcClient::AddActor(AddActorRequest& req) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    AddActorResponse res;
    auto             status = mGrpcContext->mStub->AddActor(&context, req, &res);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return res;
}

DoActionResponse ScenarioGrpcClient::DoAction(DoActionRequest& req) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    DoActionResponse res;
    auto             status = mGrpcContext->mStub->DoAction(&context, req, &res);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return res;
}

const std::string& ScenarioGrpcClient::getId() const {
    return mId;
}

GetActorPosesResponse ScenarioGrpcClient::getActorPoses(GetActorPosesRequest& req) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    GetActorPosesResponse response;
    auto                  status = mGrpcContext->mStub->GetActorPoses(&context, req, &response);

    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

SetActorPosesResponse ScenarioGrpcClient::setActorPoses(SetActorPosesRequest& req) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    SetActorPosesResponse response;
    auto                  status = mGrpcContext->mStub->SetActorPoses(&context, req, &response);
    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return response;
}

GetGeodeticCoordinateResponse ScenarioGrpcClient::getGeodeticCoordinates(GetGeodeticCoordinateRequest& req) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    GetGeodeticCoordinateResponse resp;
    auto                          status = mGrpcContext->mStub->GetGeodeticCoordinate(&context, req, &resp);

    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return resp;
}

GetRayIntersectionPointResponse ScenarioGrpcClient::getRayIntersectionPoint(GetRayIntersectionPointRequest& req) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    GetRayIntersectionPointResponse resp;
    auto                            status = mGrpcContext->mStub->GetRayIntersectionPoint(&context, req, &resp);

    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return resp;
}

ConvertPoseFormatResponse ScenarioGrpcClient::getDSDPose(ConvertPoseFormatRequest& req) {
    grpc::ClientContext context;
    rrProtoGrpc::Util::InitContext(context);

    ConvertPoseFormatResponse resp;
    auto                      status = mGrpcContext->mStub->ConvertPoseFormat(&context, req, &resp);

    if (!status.ok()) {
        throw fl::except::MakeException(ssm::scenariosimulation::ErrorFromServer(status.error_message()));
    }
    return resp;
}

} // namespace scenariogrpc
