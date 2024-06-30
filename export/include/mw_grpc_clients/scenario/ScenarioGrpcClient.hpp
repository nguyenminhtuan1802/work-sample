/* Copyright 2021-2023 The MathWorks, Inc. */
#pragma once

#include "mw_grpc_clients/mw_grpc_clients_spec.hpp"

#include <atomic>
#include <memory>
#include <string>

#include <google/protobuf/repeated_field.h>

// protobuf forward declarations
namespace mathworks { namespace scenario { namespace simulation {
class RegisterClientResponse;
class UploadMapResponse;
class UploadMapRequest;
class DownloadMapResponse;
class UploadScenarioResponse;
class UploadScenarioRequest;
class DownloadScenarioResponse;
class AddActorRequest;
class AddActorResponse;
class DoActionRequest;
class DoActionResponse;
class AddDiagnosticMessageResponse;
class AddDiagnosticMessageRequest;
class SetRuntimeActorsResponse;
class SetRuntimeActorsRequest;
class GetActorsResponse;
class NotifyActionCompleteRequest;
class SendEventsRequest;
class ReceiveEventsResponse;
class GetSimulationSettingsResponse;
class StartSimulationResponse;
class StartSimulationRequest;
class StopSimulationResponse;
class StopSimulationRequest;
class StopSimulationResponse;
class StopSimulationRequest;
class StepSimulationResponse;
class StepSimulationRequest;
class SetSimulationSettingsResponse;
class SetSimulationSettingsRequest;
class GetSimulationStatusResponse;
class GetSimulationTimeResponse;
class GetSimulationStatusRequest;
class SetSimulationPaceResponse;
class SetSimulationPaceRequest;
class RestartSimulationResponse;
class RestartSimulationRequest;
class ToggleSimulationPausedResponse;
class ToggleSimulationPausedRequest;
class SetReadyResponse;
class QueryDiagnosticMessageLogResponse;
class Event;
class QueryWorldRuntimeLogResponse;
class GetPhaseStatusResponse;
class GetActorPosesResponse;
class GetActorPosesRequest;
class SetActorPosesRequest;
class SetActorPosesResponse;
class GetGeodeticCoordinateRequest;
class GetGeodeticCoordinateResponse;
class GetWorldRuntimeLoggingStatusResponse;
class GetRayIntersectionPointRequest;
class GetRayIntersectionPointResponse;
class ConvertPoseFormatRequest;
class ConvertPoseFormatResponse;
class EnableScenarioCoverageLoggingRequest;
class EnableScenarioCoverageLoggingResponse;
class GetScenarioCoverageLoggingStatusRequest;
class GetScenarioCoverageLoggingStatusResponse;
class QueryScenarioCoverageLogRequest;
class QueryScenarioCoverageLogResponse;
}}} // namespace mathworks::scenario::simulation

namespace scenariogrpc {
// Client Id - exactly the same as RoadRunner side
struct CosimGrpcContext;

class MW_GRPC_CLIENTS_EXPORT_CLASS ScenarioGrpcClient final {
  public:
    ScenarioGrpcClient();
    ~ScenarioGrpcClient();

    // Subscribe to the gRPC event reader
    void subscribeEvents(bool synchronous, bool simulator, double timeout);

    bool readEvent(mathworks::scenario::simulation::Event* event);

    // Connects to the server at the given address and returns client registration response
    mathworks::scenario::simulation::RegisterClientResponse connect(const std::string& address, const std::string& proposedClientId);

    // Returns the unique client id for this client
    const std::string& getId() const;

    // Returns the timeout for this client
    double getTimeout() const;

    // Shut down the gRPC client
    void shutdown();

    // Returns if the gRPC client is shutdown
    bool isShutDown() const;

    // Upload the HD Map
    bool uploadMap(mathworks::scenario::simulation::UploadMapRequest& upMapReq);

    // Download the HD Map
    mathworks::scenario::simulation::DownloadMapResponse downloadMap();

    // Uploads a scenario to the server and returns the server response
    // 'uploadStatus' is a output which represents whether the upload succeeded or not
    mathworks::scenario::simulation::UploadScenarioResponse uploadScenario(mathworks::scenario::simulation::UploadScenarioRequest& upLoadReq, bool& uploadStatus);

    // Downloads the scenario from the server and returns the scenario
    mathworks::scenario::simulation::DownloadScenarioResponse downloadScenario();

    // Add diagnostic to SSE
    mathworks::scenario::simulation::AddDiagnosticMessageResponse addDiagnosticMessage(mathworks::scenario::simulation::AddDiagnosticMessageRequest& messageReq);

    // Updates the actor's state in the server and returns the server response
    mathworks::scenario::simulation::SetRuntimeActorsResponse setRuntimeActors(mathworks::scenario::simulation::SetRuntimeActorsRequest& actorsReq);

    // Returns the current state of all the actors (with a one epoch delay)
    mathworks::scenario::simulation::GetActorsResponse getActors();

    // Notify action complete events
    void notifyActionCompleteEvents(
        const google::protobuf::RepeatedPtrField<mathworks::scenario::simulation::NotifyActionCompleteRequest>& events);

    void sendEvents(
        const mathworks::scenario::simulation::SendEventsRequest& events);

    mathworks::scenario::simulation::ReceiveEventsResponse receiveEvents();

    // Returns the simulation settings
    mathworks::scenario::simulation::GetSimulationSettingsResponse getSimulationSettings();

    // Send a request to the server to get the simulation time
    mathworks::scenario::simulation::GetSimulationTimeResponse getSimulationTime();

    // Sends a request to the server to start simulation and returns the server's response
    mathworks::scenario::simulation::StartSimulationResponse startSimulation(mathworks::scenario::simulation::StartSimulationRequest& req);

    // Sends a request to the server to stop simulation and returns the server's response
    mathworks::scenario::simulation::StopSimulationResponse stopSimulation(mathworks::scenario::simulation::StopSimulationRequest& request);

    // Sends a request to the server to stop simulation because client requested and returns the
    // server's response
    mathworks::scenario::simulation::StopSimulationResponse stopSimulationRequested(mathworks::scenario::simulation::StopSimulationRequest& request);

    // Sends a request to the server to step simulation and returns the server's response
    mathworks::scenario::simulation::StepSimulationResponse stepSimulation(mathworks::scenario::simulation::StepSimulationRequest& request);

    // Sends a request to the server to set simulation settings and returns the server's response
    mathworks::scenario::simulation::SetSimulationSettingsResponse setSimulationSettings(mathworks::scenario::simulation::SetSimulationSettingsRequest& request);

    // Sends a request to the server to get simulation status and returns the server's response
    mathworks::scenario::simulation::GetSimulationStatusResponse getSimulationStatus(mathworks::scenario::simulation::GetSimulationStatusRequest& request);

    // Sends a request to the server to get simulation pace and returns the server's response
    mathworks::scenario::simulation::SetSimulationPaceResponse setSimulationPace(mathworks::scenario::simulation::SetSimulationPaceRequest& request);

    // Sends a request to the server to restart simulation and returns the server's response
    mathworks::scenario::simulation::RestartSimulationResponse restartSimulation(mathworks::scenario::simulation::RestartSimulationRequest& request);

    // Sends a request to the server to restart simulation and returns the server's response
    mathworks::scenario::simulation::ToggleSimulationPausedResponse pauseSimulation(mathworks::scenario::simulation::ToggleSimulationPausedRequest& request);

    // Informs the server that the client is done processing the current event
    mathworks::scenario::simulation::SetReadyResponse setClientReady();

    // Get the diagnostics log response
    mathworks::scenario::simulation::QueryDiagnosticMessageLogResponse QueryDiagnosticMessageLog();

    // Get actor runtime log
    mathworks::scenario::simulation::QueryWorldRuntimeLogResponse QueryWorldRuntimeLog();

    // Get actor diagnostics log
    mathworks::scenario::simulation::QueryDiagnosticMessageLogResponse QueryDiagnosticLog();

    // Get phase status
    mathworks::scenario::simulation::GetPhaseStatusResponse GetPhaseStatus();

    // Add actor
    mathworks::scenario::simulation::AddActorResponse AddActor(mathworks::scenario::simulation::AddActorRequest& req);

    // Do action
    mathworks::scenario::simulation::DoActionResponse DoAction(mathworks::scenario::simulation::DoActionRequest& req);

    // get actor poses in specified Coordinate reference system
    mathworks::scenario::simulation::GetActorPosesResponse getActorPoses(mathworks::scenario::simulation::GetActorPosesRequest& req);

    // set actor poses in specified Coordinate reference system
    mathworks::scenario::simulation::SetActorPosesResponse setActorPoses(mathworks::scenario::simulation::SetActorPosesRequest& req);

    // service to convert given position coordinates to geodetic coordinates.
    mathworks::scenario::simulation::GetGeodeticCoordinateResponse getGeodeticCoordinates(mathworks::scenario::simulation::GetGeodeticCoordinateRequest& req);

    // service to get intersection point for a ray sent in a particular direction on the mesh
    mathworks::scenario::simulation::GetRayIntersectionPointResponse getRayIntersectionPoint(mathworks::scenario::simulation::GetRayIntersectionPointRequest& req);

    // service to convert format
    mathworks::scenario::simulation::ConvertPoseFormatResponse getDSDPose(mathworks::scenario::simulation::ConvertPoseFormatRequest& req);

    // Turn on/off actor runtime logging
    bool EnableWorldRuntimeLogging(bool);

    // Get the world runtime logging
    mathworks::scenario::simulation::GetWorldRuntimeLoggingStatusResponse GetWorldRuntimeLoggingStatus();

    // Turn on/off scenario coverage logging
    bool EnableScenarioCoverageLogging(bool isOn);

    // Get the scenario coverage logging status
    mathworks::scenario::simulation::GetScenarioCoverageLoggingStatusResponse GetScenarioCoverageLoggingStatus();

    // Query the server for scenario coverage log
    mathworks::scenario::simulation::QueryScenarioCoverageLogResponse QueryScenarioCoverageLog();

  private:
    std::unique_ptr<CosimGrpcContext> mGrpcContext;

    // Client Id
    std::string mId;

    // The timeout for all events, unit is second
    // Users can change it in MATLAB by settings :
    // settings.roadrunner.application.Timeout.TemporaryValue = 200
    double mClientTimeout;

    // the flag indicates whether this client is shutting down
    std::atomic<bool> mIsShutDown;
};

} // namespace scenariogrpc
