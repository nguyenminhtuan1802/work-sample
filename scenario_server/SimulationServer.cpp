#include "SimulationServer.h"

#include "Util.h"
// #include "ScenarioEngine.h"
#include "Simulation.h"
#include "Utility.h"
// #include "BootstrapManager.h"
#include "MessageLogger.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

using namespace sseProto;

////////////////////////////////////////////////////////////////////////////////

namespace {
// Convenience functions
inline std::string GetNewQUuid() {
    auto uuid = boost::uuids::random_generator()();
    return boost::uuids::to_string(uuid);
}
} // namespace

////////////////////////////////////////////////////////////////////////////////

namespace sse {

SimulationServer::SimulationServer(const std::string& address, int port) {
    /*if (!disableLicensingCheck) {
            rrThrowVerify(rrLicenseMgr::QueryFeature(rrProgramFeature::eScenario),
                    "Unable to start Scenario Server: %1"_Q % rrLicenseMgr::
                    BuildMissingFeatureMessage(rrProgramFeature::eScenario));
    }*/

    // Set the port number back, so clients can pick up
    // rrProtoGrpc::Ports::SetCoSimPort(port);

    // Start gRPC scenario server
    mServerThread = rrProtoGrpc::Util::StartServer<SimulationServer>(
        mSimulationServer, this, rrProtoGrpc::Util::CreateAddress(address, port));

    rrPrint("Started Simulation API server on port: " + std::to_string(port));

    // Create event publisher
    mPublisher = std::make_unique<SimulationEventPublisher>(this);

    // Create Bootstrap manager to initiate MATLAB / CARLA process on demand
    // mBootstrapManager = std::make_unique<BootstrapManager>(this, port);

    // Create scenario simulation
    mSimulation = std::make_unique<Simulation>(mPublisher.get());

    // Instantiate the message logger
    mMessageLogger = std::make_unique<MessageLogger>(mSimulation.get());

    // Instantiate the actor runtime logger
    mActorRuntimeLogger = std::make_unique<ActorRunTimeLogger>(mSimulation.get());

    // Instantiate the diagnostic message logger
    mDiagnosticMessageLogger = std::make_unique<sse::DiagnosticMessageLogger>(mSimulation.get());

    // Set the logger on the publisher
    mPublisher->SetMessageLogger(mMessageLogger.get());

    // Set the runtime logger on the publisher
    mPublisher->SetRuntimeLogger(mActorRuntimeLogger.get());

    // Connect actor runtime logger to simulation
    mSimulation->SetActorRuntimeLogger(mActorRuntimeLogger.get());
}

////////////////////////////////////////////////////////////////////////////////

SimulationServer::~SimulationServer() {
    // Destroy scenario simulation engine
    mSimulation.reset();

    // Shutdown event publisher
    if (mPublisher) {
        // Notify clients that the server will shutdown
        auto evt = EventCalendar::CreateServerShutdownEvent();
        mPublisher->BroadcastEvent(*evt);

        // Cancel all subscribers
        mPublisher->CancelSubscribers();
    }

    // Notify each bootstrap client for shutdown
    /*if (mBootstrapManager) {
            mBootstrapManager->NotifyBootstrapClientServerShutDown();
    }*/

    // Shutdown gRPC server
    if (mSimulationServer) {
        rrPrint("Shutting Down Simulation API Server.");
        mCanceled = true;
        mSimulationServer->Shutdown();
        // Wait for server thread to complete
        if (mServerThread.valid()) {
            mServerThread.get();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void SimulationServer::SetInstantiator(SSEServerInstantiator* instantiator) {
    mSimulation->SetInstantiator(instantiator);
}

////////////////////////////////////////////////////////////////////////////////

void SimulationServer::NotifySimStatusChanged(SimulationStatus status) {
    // Do nothing if the server is not owned by any co-simulation manager
    /*if (mOwner == nullptr)
            return;

    switch (status) {
    case SimulationStatus::SIMULATION_STATUS_STOPPED:
            mOwner->DoPostSimulationUpdate();
            break;
    default:
            break;
    }*/
}

////////////////////////////////////////////////////////////////////////////////

bool SimulationServer::SimulationRunning() const {
    rrVerify(mSimulation);
    return mSimulation->IsSimulationRunning();
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::RegisterClient(::grpc::ServerContext*, const RegisterClientRequest* request, RegisterClientResponse* response) {
    // if applicable, use proposed client id per client's request
    std::string clientID = request->proposed_client_id();
    if (clientID.empty()) {
        // otherwise we generate new id for the client
        clientID = GetNewQUuid();
    }

    // If logging is enabled, log this rpc call
    if (mEnableCommLogging) {
        mMessageLogger->AddRPCMessage(*request, clientID, Result());
    }
    // Calling AddRPCMessage() directly, unable to add client metadata on ServerContext

    // Add client to subscribers in event publisher
    try {
        mPublisher->RegisterClient(request, clientID);
    } catch (const rrException& e) {
        return {::grpc::StatusCode::UNKNOWN, e.ToString()};
    }

    response->set_client_id(clientID);
    rrDev("Registered client '" + request->name() + "' with client id: " + clientID);

    return ::grpc::Status::OK;
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::SetReady(::grpc::ServerContext* context, const SetReadyRequest* request, SetReadyResponse*) {
    ::grpc::Status status = grpc::Status::OK;

    std::string clientID = request->client_id();

    // Notify event publisher that the client is ready
    if (!mPublisher->SetClientReady(clientID)) {
        status = ::grpc::Status(::grpc::StatusCode::FAILED_PRECONDITION,
                                "Client ID: " + clientID +
                                    " is either not an event subscriber or has already set status as ready. Subscribe by invoking 'SubscribeEvents'.");
    }

    AddMessageToLogger(*request, *context, status.ok() ? Result() : Result(status.error_message()));

    return status;
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::SetBusy(::grpc::ServerContext* context, const SetBusyRequest* request, SetBusyResponse*) {
    mPublisher->SetClientBusy(request->client_id(), request->status());
    AddMessageToLogger(*request, *context, Result());
    return ::grpc::Status::OK;
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::SubscribeEvents(::grpc::ServerContext* context, const SubscribeEventsRequest* request, ::grpc::ServerWriter<Event>* writer) {
    std::string clientID = request->client_id();

    // Subscribe for events with the EventPublisher
    try {
        mPublisher->AddSubscriber(*request, context, writer);

        // Subscriber is a bootstrapper; notify booting thread and finish-up
        /*if (mBootstrapManager->IsBootstrapClient(clientID)) {
                mBootstrapManager->NotifyBootstrapClientAvailable(clientID);
        }*/
    } catch (const rrException& e) {
        return {::grpc::StatusCode::UNKNOWN, e.ToString()};
    }

    // Add the client to message logger
    // mMessageLogger->RegisterEventSubscriberID(clientID);

    // Log the RPC request message
    AddMessageToLogger(*request, *context, Result());

    while (!context->IsCancelled() && !mCanceled) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Remove the entry from the map
    mPublisher->RemoveSubscriber(clientID);

    // Clear the cached platform client Id
    // mBootstrapManager->NotifyBootstrapClientUnsubscribe(clientID);

    // Remove entry from the message logger
    mMessageLogger->UnregisterEventSubscriberID(clientID);

    return ::grpc::Status::OK;
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::UploadMap(::grpc::ServerContext* context, const UploadMapRequest* request, UploadMapResponse* response) {
    return DelegateCommand(&Simulation::UploadMap, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::DownloadMap(::grpc::ServerContext* context, const DownloadMapRequest* request, DownloadMapResponse* response) {
    return DelegateCommand(&Simulation::DownloadMap, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::UploadScenario(::grpc::ServerContext* context, const UploadScenarioRequest* request, UploadScenarioResponse* response) {
    return DelegateCommand(&Simulation::UploadScenario, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::DownloadScenario(::grpc::ServerContext* context, const DownloadScenarioRequest* request, DownloadScenarioResponse* response) {
    return DelegateCommand(&Simulation::DownloadScenario, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::SetSimulationSettings(::grpc::ServerContext* context, const SetSimulationSettingsRequest* request, SetSimulationSettingsResponse* response) {
    return DelegateCommand(&Simulation::SetSimulationSettings, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::SetSimulationPace(::grpc::ServerContext* context, const SetSimulationPaceRequest* request, SetSimulationPaceResponse* response) {
    return DelegateCommand(&Simulation::SetSimulationPace, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::GetSimulationSettings(::grpc::ServerContext* context, const GetSimulationSettingsRequest* request, GetSimulationSettingsResponse* response) {
    return DelegateCommand(&Simulation::GetSimulationSettings, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::StartSimulation(::grpc::ServerContext* context, const StartSimulationRequest* request, StartSimulationResponse* response) {
    // Call the scenario engine start
    return DelegateCommand(&Simulation::StartSimulation, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::RestartSimulation(::grpc::ServerContext* context, const RestartSimulationRequest* request, RestartSimulationResponse* response) {
    return DelegateCommand(&Simulation::RestartSimulation, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::StopSimulation(::grpc::ServerContext* context, const StopSimulationRequest* request, StopSimulationResponse* response) {
    return DelegateCommand(&Simulation::StopSimulation, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::StopSimulationRequested(::grpc::ServerContext* context, const StopSimulationRequest* request, StopSimulationResponse* response) {
    return DelegateCommand(&Simulation::StopSimulationRequested, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::StepSimulation(::grpc::ServerContext* context, const StepSimulationRequest* request, StepSimulationResponse* response) {
    return DelegateCommand(&Simulation::StepSimulation, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::ToggleSimulationPaused(::grpc::ServerContext* context, const ToggleSimulationPausedRequest* request, ToggleSimulationPausedResponse* response) {
    return DelegateCommand(&Simulation::PauseOrResumeSimulation, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::SendEvents(::grpc::ServerContext* context, const SendEventsRequest* request, SendEventsResponse* response) {
    return DelegateCommand(&Simulation::SendEvents, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::ReceiveEvents(::grpc::ServerContext* context, const ReceiveEventsRequest* request, ReceiveEventsResponse* response) {
    return DelegateCommand(&Simulation::ReceiveEvents, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::GetSimulationStatus(::grpc::ServerContext* context, const ::mathworks::scenario::simulation::GetSimulationStatusRequest* request, ::mathworks::scenario::simulation::GetSimulationStatusResponse* response) {
    return DelegateCommand(&Simulation::GetSimulationStatus, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::GetSimulationTime(::grpc::ServerContext* context, const ::mathworks::scenario::simulation::GetSimulationTimeRequest* request, ::mathworks::scenario::simulation::GetSimulationTimeResponse* response) {
    return DelegateCommand(&Simulation::GetSimulationTime, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::SetRuntimeActors(::grpc::ServerContext* context, const SetRuntimeActorsRequest* request, SetRuntimeActorsResponse* response) {
    return DelegateCommand(&Simulation::SetRuntimeActors, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::SetActorPoses(::grpc::ServerContext* context, const SetActorPosesRequest* request, SetActorPosesResponse* response) {
    return DelegateCommand(&Simulation::SetActorPoses, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::SetVehiclePoses(::grpc::ServerContext* context, const SetVehiclePosesRequest* request, SetVehiclePosesResponse* response) {
    return DelegateCommand(&Simulation::SetVehiclePoses, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::GetRuntimeActors(::grpc::ServerContext* context, const GetRuntimeActorsRequest* request, GetRuntimeActorsResponse* response) {
    return DelegateCommand(&Simulation::GetRuntimeActors, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::GetActors(::grpc::ServerContext* context, const GetActorsRequest* request, GetActorsResponse* response) {
    return DelegateCommand(&Simulation::GetActors, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::GetActorPoses(::grpc::ServerContext* context, const GetActorPosesRequest* request, GetActorPosesResponse* response) {
    return DelegateCommand(&Simulation::GetActorPoses, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::GetGeodeticCoordinate(::grpc::ServerContext* context, const GetGeodeticCoordinateRequest* request, GetGeodeticCoordinateResponse* response) {
    return DelegateCommand(&Simulation::GetGeodeticCoordinate, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::NotifyActionComplete(::grpc::ServerContext* context, const NotifyActionCompleteRequest* request, NotifyActionCompleteResponse* response) {
    return DelegateCommand(&Simulation::NotifyActionComplete, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::GetPhaseStatus(::grpc::ServerContext* context, const GetPhaseStatusRequest* request, GetPhaseStatusResponse* response) {
    return DelegateCommand(&Simulation::GetPhaseStatus, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::AddActor(::grpc::ServerContext* context, const AddActorRequest* request, AddActorResponse* response) {
    return DelegateCommand(&Simulation::AddRuntimeActor, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::DoAction(::grpc::ServerContext* context, const DoActionRequest* request, DoActionResponse* response) {
    return DelegateCommand(&Simulation::DoAction, mSimulation, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::QueryCommunicationLog(::grpc::ServerContext* context, const QueryCommunicationLogRequest* request, QueryCommunicationLogResponse* response) {
    return DelegateCommand(&MessageLogger::RetrieveMessages, mMessageLogger, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::EnableCommunicationLogging(::grpc::ServerContext* context, const EnableCommunicationLoggingRequest* request, EnableCommunicationLoggingResponse* response) {
    // Check if simulation has started
    if (mSimulation->IsSimulationRunning()) {
        return {::grpc::StatusCode::UNKNOWN, "Logging cannot be enabled/disabled while simulation is already running."};
    }

    // Set the logging flag
    mEnableCommLogging = request->log_communication();

    // Set the buffer length on the message logger
    return DelegateCommand(&MessageLogger::SetBufferLength, mMessageLogger, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::QueryWorldRuntimeLog(::grpc::ServerContext* context, const QueryWorldRuntimeLogRequest* request, QueryWorldRuntimeLogResponse* response) {
    return DelegateCommand(&ActorRunTimeLogger::RetrieveMessages, mActorRuntimeLogger, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::EnableWorldRuntimeLogging(::grpc::ServerContext* context, const EnableWorldRuntimeLoggingRequest* request, EnableWorldRuntimeLoggingResponse* response) {
    // Check if simulation has started
    if (mSimulation->IsSimulationRunning()) {
        return {::grpc::StatusCode::UNKNOWN, "Logging cannot be enabled/disabled while simulation is already running."};
    }

    return DelegateCommand(&ActorRunTimeLogger::EnableActorRuntimeLogging, mActorRuntimeLogger, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::AddDiagnosticMessage(::grpc::ServerContext* context, const AddDiagnosticMessageRequest* request, AddDiagnosticMessageResponse* response) {
    auto status = DelegateCommand(&DiagnosticMessageLogger::AddDiagnosticMessages, mDiagnosticMessageLogger, *context, *request, *response);

    // Request simulation to stop if a simulation is running and an error is received
    int num = request->diagnostic_messages_size();
    for (int i = 0; i < num; ++i) {
        const auto& msg = request->diagnostic_messages(i);
        if (msg.diagnostic_type() == DiagnosticType::ERROR_TYPE &&
            mSimulation->IsSimulationRunning()) {
            // Request simulation to stop with
            // - Cause: client reports an error
            // - Summary: the error message
            StopSimulationRequest  stopReq;
            StopSimulationResponse stopRes;
            auto*                  cause = stopReq.mutable_cause();
            cause->set_summary("Error(s) reported by client.");
            cause->mutable_simulation_failed()->mutable_client_error();
            mSimulation->StopSimulationRequested(stopReq, stopRes);
            break;
        }
    }

    return ::grpc::Status::OK;
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::QueryDiagnosticMessageLog(::grpc::ServerContext* context, const QueryDiagnosticMessageLogRequest* request, QueryDiagnosticMessageLogResponse* response) {
    return DelegateCommand(&DiagnosticMessageLogger::RetrieveMessages, mDiagnosticMessageLogger, *context, *request, *response);
}

////////////////////////////////////////////////////////////////////////////////

::grpc::Status SimulationServer::TranslateResult(Result& res) {
    if (res.Type == Result::EnumType::eSuccess) {
        return ::grpc::Status::OK;
    } else {
        throw rrException(res.Message);
    }
}

////////////////////////////////////////////////////////////////////////////////

void SimulationServer::AddMessageToLogger(const ::google::protobuf::Message& request,
                                          const ::grpc::ServerContext&       context,
                                          const Result&                      result) {
    // If logging is enabled, log the RPC requests
    if (mEnableCommLogging) {
        // Retrieve the client id from the context
        std::string clientID("UnknownClient");
        auto&       multiMap = context.client_metadata();
        auto        iter     = multiMap.find("client_id");
        if (iter != multiMap.end()) {
            clientID = std::string(iter->second.begin(), iter->second.end());
        }

        // Log RPC request
        mMessageLogger->AddRPCMessage(request, clientID, result);
    }
}

////////////////////////////////////////////////////////////////////////////////
} // namespace sse
