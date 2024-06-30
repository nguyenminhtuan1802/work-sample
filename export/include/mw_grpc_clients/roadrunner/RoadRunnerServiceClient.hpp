/* Copyright 2021-2023 The MathWorks, Inc.

RoadRunnerServiceClient calls the RoadRunner API using the gRPC stub.

*/
#pragma once

#include "mw_grpc_clients/mw_grpc_clients_spec.hpp"

#include <string>
#include <memory>

namespace grpc {
class Status;
}

namespace mathworks::roadrunner {
class GetVariableRequest;
class GetVariableResponse;
class GetAllVariablesRequest;
class GetAllVariablesResponse;
class RoadRunnerStatusResponse;
class RoadRunnerStatusRequest;
class NewProjectRequest;
class LoadProjectRequest;
class SaveProjectRequest;
class NewSceneRequest;
class LoadSceneRequest;
class SaveSceneRequest;
class ExportRequest;
class ImportRequest;
class ExitRequest;
class NewScenarioRequest;
class LoadScenarioRequest;
class SaveScenarioRequest;
class SetVariableRequest;
class PrepareSimulationRequest;
class SimulateScenarioRequest;
class RemapAnchorRequest;
class ChangeWorldSettingsRequest;
class ChangeWorldSettingsResponse;
class GetAnchorsRequest;
class GetAnchorsResponse;
} // namespace mathworks::roadrunner

namespace rrProto = mathworks::roadrunner;

namespace roadrunnerapi {

struct RoadRunnerGrpcContext;

class MW_GRPC_CLIENTS_EXPORT_CLASS RoadRunnerServiceClient final {
  public:
    RoadRunnerServiceClient();
    ~RoadRunnerServiceClient();

    void Connect(int apiPort = 35707);

    double GetApiPort() const {
        return mApiPort;
    };

    bool IsServerReady() const;

    rrProto::GetVariableResponse GetScenarioVariable(const rrProto::GetVariableRequest* request) const;

    rrProto::RoadRunnerStatusResponse RoadRunnerStatus(const rrProto::RoadRunnerStatusRequest* request) const;

    rrProto::GetAllVariablesResponse GetAllScenarioVariables(const rrProto::GetAllVariablesRequest* request) const;

    rrProto::ChangeWorldSettingsResponse ChangeWorldSettings(const rrProto::ChangeWorldSettingsRequest* request) const;
    rrProto::GetAnchorsResponse          GetAnchors(const rrProto::GetAnchorsRequest* request) const;

    // Call a RoadRunnerService method
    void NewProject(const rrProto::NewProjectRequest* request) const;
    void LoadProject(const rrProto::LoadProjectRequest* request) const;
    void SaveProject(const rrProto::SaveProjectRequest* request) const;
    void NewScene(const rrProto::NewSceneRequest* request) const;
    void LoadScene(const rrProto::LoadSceneRequest* request) const;
    void SaveScene(const rrProto::SaveSceneRequest* request) const;
    void Export(const rrProto::ExportRequest* request) const;
    void Import(const rrProto::ImportRequest* request) const;
    void Exit(const rrProto::ExitRequest* request) const;
    void NewScenario(const rrProto::NewScenarioRequest* request) const;
    void LoadScenario(const rrProto::LoadScenarioRequest* request) const;
    void SaveScenario(const rrProto::SaveScenarioRequest* request) const;
    void SetScenarioVariable(const rrProto::SetVariableRequest* request) const;
    void PrepareSimulation(const rrProto::PrepareSimulationRequest* request) const;
    void SimulateScenario(const rrProto::SimulateScenarioRequest* request) const;
    void RemapAnchor(const rrProto::RemapAnchorRequest* request) const;

  private:
    std::unique_ptr<RoadRunnerGrpcContext> mGrpcContext;
    int                                    mApiPort;
};

} // namespace roadrunnerapi
