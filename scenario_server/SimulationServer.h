#pragma once

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "RoadRunnerConversions.h"
#include "EventPublisher.h"

#include "mathworks/scenario/scene/hd/hd_map.pb.h"
#include "mathworks/scenario/simulation/cosim_api.grpc.pb.h"
#include "mathworks/scenario/simulation/event.pb.h"
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/security/server_credentials.h>

#include <future>
#include <thread>

namespace sse {
class ActorRunTimeLogger;
class BootstrapManager;
class DiagnosticMessageLogger;
class MessageLogger;
class Result;
class Simulation;
class SimulationService;
class SSEServerInstantiator;
} // namespace sse

////////////////////////////////////////////////////////////////////////////////
// gRPC server class for orchestrating a co-simulation. This class in conjunction
// with EventPublisher, keeps track of simulation clients and publishes simulation
// events such as, simulation start, pause, stop etc.

namespace sse {

class SimulationServer : public sseProto::CoSimulationApi::Service {
    VZ_DECLARE_NONCOPYABLE(SimulationServer);

  public:
    using EventWriter = ::grpc::ServerWriter<sseProto::Event>;

    SimulationServer(const std::string& address, int port);
    ~SimulationServer();

    void SetOwner(SimulationService* owner) {
        mOwner = owner;
    }

    void SetInstantiator(SSEServerInstantiator* instantiator);

    void NotifySimStatusChanged(sseProto::SimulationStatus status);

    EventPublisher* GetEventPublisher() {
        return mPublisher.get();
    }

    Simulation* GetSimulation() {
        return mSimulation.get();
    }

    bool SimulationRunning() const;

    bool IsLoggingEnabled() {
        return mEnableCommLogging;
    }

  private:
    //////////////////////////////////////
    // Client connection and subscriptions
    //////////////////////////////////////

    virtual ::grpc::Status RegisterClient(::grpc::ServerContext* context, const sseProto::RegisterClientRequest* request, sseProto::RegisterClientResponse* response) override;

    virtual ::grpc::Status SetReady(::grpc::ServerContext* context, const sseProto::SetReadyRequest* request, sseProto::SetReadyResponse* response) override;

    virtual ::grpc::Status SetBusy(::grpc::ServerContext* context, const sseProto::SetBusyRequest* request, sseProto::SetBusyResponse* response) override;

    virtual ::grpc::Status SubscribeEvents(::grpc::ServerContext* context, const sseProto::SubscribeEventsRequest* request, ::grpc::ServerWriter<sseProto::Event>* writer) override;

    //////////////////////////////////////
    // Scene and map access interface
    //////////////////////////////////////

    virtual ::grpc::Status UploadMap(::grpc::ServerContext* context, const sseProto::UploadMapRequest* request, sseProto::UploadMapResponse* response) override;

    virtual ::grpc::Status DownloadMap(::grpc::ServerContext* context, const sseProto::DownloadMapRequest* request, sseProto::DownloadMapResponse* response) override;

    //////////////////////////////////////
    // Scenario access interface
    //////////////////////////////////////

    virtual ::grpc::Status UploadScenario(::grpc::ServerContext* context, const sseProto::UploadScenarioRequest* request, sseProto::UploadScenarioResponse* response) override;

    virtual ::grpc::Status DownloadScenario(::grpc::ServerContext* context, const sseProto::DownloadScenarioRequest* request, sseProto::DownloadScenarioResponse* response) override;

    //////////////////////////////////////
    // Simulation settings access interface
    //////////////////////////////////////

    virtual ::grpc::Status SetSimulationSettings(::grpc::ServerContext* context, const sseProto::SetSimulationSettingsRequest* request, sseProto::SetSimulationSettingsResponse* response) override;

    // Request the server the change the pace of simulation
    virtual ::grpc::Status SetSimulationPace(::grpc::ServerContext* context, const sseProto::SetSimulationPaceRequest* request, sseProto::SetSimulationPaceResponse* response) override;

    virtual ::grpc::Status GetSimulationSettings(::grpc::ServerContext* context, const sseProto::GetSimulationSettingsRequest* request, sseProto::GetSimulationSettingsResponse* response) override;

    //////////////////////////////////////
    // Simulation control interface
    //////////////////////////////////////

    virtual ::grpc::Status StartSimulation(::grpc::ServerContext* context, const sseProto::StartSimulationRequest* request, sseProto::StartSimulationResponse* response) override;

    virtual ::grpc::Status RestartSimulation(::grpc::ServerContext* context, const sseProto::RestartSimulationRequest* request, sseProto::RestartSimulationResponse* response) override;

    virtual ::grpc::Status StopSimulation(::grpc::ServerContext* context, const sseProto::StopSimulationRequest* request, sseProto::StopSimulationResponse* response) override;

    // A non-blocking call to request for the simulation to stop
    virtual ::grpc::Status StopSimulationRequested(::grpc::ServerContext* context, const sseProto::StopSimulationRequest* request, sseProto::StopSimulationResponse* response) override;

    virtual ::grpc::Status StepSimulation(::grpc::ServerContext* context, const sseProto::StepSimulationRequest* request, sseProto::StepSimulationResponse* response) override;

    virtual ::grpc::Status ToggleSimulationPaused(::grpc::ServerContext* context, const sseProto::ToggleSimulationPausedRequest* request, sseProto::ToggleSimulationPausedResponse* response) override;

    // Get current simulation status
    virtual ::grpc::Status GetSimulationStatus(::grpc::ServerContext* context, const sseProto::GetSimulationStatusRequest* request, sseProto::GetSimulationStatusResponse* response) override;

    // Get current simulation time
    virtual ::grpc::Status GetSimulationTime(::grpc::ServerContext* context, const sseProto::GetSimulationTimeRequest* request, sseProto::GetSimulationTimeResponse* response) override;

    //////////////////////////////////////
    // World states access interface
    //////////////////////////////////////

    virtual ::grpc::Status SetRuntimeActors(::grpc::ServerContext* context, const sseProto::SetRuntimeActorsRequest* request, sseProto::SetRuntimeActorsResponse* response) override;

    virtual ::grpc::Status SetActorPoses(::grpc::ServerContext* context, const sseProto::SetActorPosesRequest* request, sseProto::SetActorPosesResponse* response) override;

    virtual ::grpc::Status SetVehiclePoses(::grpc::ServerContext* context, const sseProto::SetVehiclePosesRequest* request, sseProto::SetVehiclePosesResponse* response) override;

    virtual ::grpc::Status GetRuntimeActors(::grpc::ServerContext* context, const sseProto::GetRuntimeActorsRequest* request, sseProto::GetRuntimeActorsResponse* response) override;

    virtual ::grpc::Status GetActors(::grpc::ServerContext* context, const sseProto::GetActorsRequest* request, sseProto::GetActorsResponse* response) override;

    virtual ::grpc::Status GetActorPoses(::grpc::ServerContext* context, const sseProto::GetActorPosesRequest* request, sseProto::GetActorPosesResponse* response) override;

    virtual ::grpc::Status GetGeodeticCoordinate(::grpc::ServerContext* context, const sseProto::GetGeodeticCoordinateRequest* request, sseProto::GetGeodeticCoordinateResponse* response) override;

    virtual ::grpc::Status NotifyActionComplete(::grpc::ServerContext* context, const sseProto::NotifyActionCompleteRequest* request, sseProto::NotifyActionCompleteResponse* response) override;

    virtual ::grpc::Status SendEvents(::grpc::ServerContext* context, const sseProto::SendEventsRequest* request, sseProto::SendEventsResponse* response) override;

    virtual ::grpc::Status ReceiveEvents(::grpc::ServerContext* context, const sseProto::ReceiveEventsRequest* request, sseProto::ReceiveEventsResponse* response) override;

    virtual ::grpc::Status GetPhaseStatus(::grpc::ServerContext* context, const sseProto::GetPhaseStatusRequest* request, sseProto::GetPhaseStatusResponse* response) override;

    virtual ::grpc::Status AddActor(::grpc::ServerContext* context, const sseProto::AddActorRequest* request, sseProto::AddActorResponse* response) override;

    virtual ::grpc::Status DoAction(::grpc::ServerContext* context, const sseProto::DoActionRequest* request, sseProto::DoActionResponse* response) override;

    //////////////////////////////////////
    // Simulation logging interface
    //////////////////////////////////////

    // Request the server for communication logging information
    virtual ::grpc::Status QueryCommunicationLog(::grpc::ServerContext* context, const sseProto::QueryCommunicationLogRequest* request, sseProto::QueryCommunicationLogResponse* response) override;

    // Toggle simulation logging
    virtual ::grpc::Status EnableCommunicationLogging(::grpc::ServerContext* context, const sseProto::EnableCommunicationLoggingRequest* request, sseProto::EnableCommunicationLoggingResponse* response) override;

    // Request the server for actor runtime log information
    virtual ::grpc::Status QueryWorldRuntimeLog(::grpc::ServerContext* context, const sseProto::QueryWorldRuntimeLogRequest* request, sseProto::QueryWorldRuntimeLogResponse* response);

    // Enable or disable server logging of actor runtime information
    virtual ::grpc::Status EnableWorldRuntimeLogging(::grpc::ServerContext* context, const sseProto::EnableWorldRuntimeLoggingRequest* request, sseProto::EnableWorldRuntimeLoggingResponse* response);

    // Add a diagnostic message to the server. The diagnostic message can be information, warning or error.
    virtual ::grpc::Status AddDiagnosticMessage(::grpc::ServerContext* context, const sseProto::AddDiagnosticMessageRequest* request, sseProto::AddDiagnosticMessageResponse* response);

    // Query the server for diagnostic messages. The query can filter messages based on message type, client id, or time range.
    virtual ::grpc::Status QueryDiagnosticMessageLog(::grpc::ServerContext* context, const sseProto::QueryDiagnosticMessageLogRequest* request, sseProto::QueryDiagnosticMessageLogResponse* response);

  private:
    template <typename ClassT>
    void CheckPreconditions(rrUP<ClassT>& ptr) {
        rrThrowVerify(ptr, "Internal error occurred");
    }

    ::grpc::Status TranslateResult(Result& res);

    void AddMessageToLogger(const ::google::protobuf::Message& rpcName, const ::grpc::ServerContext& context, const Result& result);

    template <typename EngineFuncPtrT, typename PtrT, typename RequestT, typename ResponseT>
    ::grpc::Status DelegateCommand(EngineFuncPtrT funcPtr, rrUP<PtrT>& classObjPtr, const ::grpc::ServerContext& context, const RequestT& request, ResponseT& response) {
        try {
            CheckPreconditions(classObjPtr);

            auto res = std::invoke(funcPtr, *classObjPtr, request, response);

            // Add message to communication message log
            AddMessageToLogger(request, context, res);

            return TranslateResult(res);
        } catch (const rrException& e) {
            return {::grpc::StatusCode::UNKNOWN, e.ToString()};
        }
    }

  private:
    rrUP<grpc::Server> mSimulationServer;
    std::future<void>  mServerThread;
    // Need an external bool since ServerThread cannot be cancelled (See Qt docs)
    std::atomic_bool mCanceled = false;

    rrUP<SimulationEventPublisher> mPublisher;
    rrUP<Simulation>               mSimulation;
    SimulationService*             mOwner = nullptr;

    rrUP<MessageLogger>           mMessageLogger;
    rrUP<DiagnosticMessageLogger> mDiagnosticMessageLogger;
    rrUP<ActorRunTimeLogger>      mActorRuntimeLogger;
    std::atomic<bool>             mEnableCommLogging = false; // By default logging is disabled

    // rrUP<BootstrapManager> mBootstrapManager;
};
} // namespace sse

////////////////////////////////////////////////////////////////////////////////
