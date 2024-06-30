/* Copyright 2021-2023 The MathWorks, Inc.

RoadRunnerExperimental calls the RoadRunner Experimental API using the gRPC stub.

*/
#pragma once

#include "mw_grpc_clients/mw_grpc_clients_spec.hpp"

#include <string>
#include <memory>

namespace mathworks::roadrunner {
class SetVariableRequest;
class SetVariableResponse;
class GetVariableRequest;
class GetVariableResponse;
class CreateRoadRequest;
class CreateRoadResponse;
class GetActiveLanesAtDistanceRequest;
class GetActiveLanesAtDistanceResponse;
class StartBatchCommandsRequest;
class StartBatchCommandsResponse;
class EndBatchCommandsRequest;
class EndBatchCommandsResponse;
class GetMeshRequest;
class CreateCommandMarkerRequest;
class CreateCommandMarkerResponse;
class UndoRequest;
class UndoResponse;
class RedoRequest;
class RedoResponse;
class CreateOutputMessageMarkerRequest;
class CreateOutputMessageMarkerResponse;
class GetOutputMessagesRequest;
class GetOutputMessagesResponse;
class AddOutputMessagesRequest;
class AddOutputMessagesResponse;
} // namespace mathworks::roadrunner

namespace rrProto = mathworks::roadrunner;

namespace roadrunnerapi {

struct RoadRunnerExperimentalGrpcContext;

class MW_GRPC_CLIENTS_EXPORT_CLASS RoadRunnerExperimental final {
  public:
    RoadRunnerExperimental();
    ~RoadRunnerExperimental();

    void Connect(int apiPort = 35707);

    double GetApiPort() const {
        return mApiPort;
    };

    bool IsServerReady() const;

    // Call a RoadRunnerExperimental method
    void                                       SetSceneVariable(const rrProto::SetVariableRequest* request) const;
    rrProto::GetVariableResponse               GetSceneVariable(const rrProto::GetVariableRequest* request) const;
    rrProto::CreateCommandMarkerResponse       CreateCommandMarker() const;
    void                                       Undo(const rrProto::UndoRequest* request) const;
    void                                       Redo(const rrProto::RedoRequest* request) const;
    rrProto::CreateOutputMessageMarkerResponse CreateOutputMessageMarker() const;
    rrProto::GetOutputMessagesResponse         GetOutputMessages(const rrProto::GetOutputMessagesRequest* request) const;
    void                                       AddOutputMessages(const rrProto::AddOutputMessagesRequest* request) const;
    rrProto::CreateRoadResponse                CreateRoad(const rrProto::CreateRoadRequest* request) const;
    rrProto::GetActiveLanesAtDistanceResponse  GetActiveLanesAtDistance(const rrProto::GetActiveLanesAtDistanceRequest* request) const;
    void                                       StartBatchCommands(const rrProto::StartBatchCommandsRequest* request) const;
    void                                       EndBatchCommands(const rrProto::EndBatchCommandsRequest* request) const;
    std::string                                GetMesh(const rrProto::GetMeshRequest* request) const;

  private:
    std::unique_ptr<RoadRunnerExperimentalGrpcContext> mGrpcContext;
    int                                                mApiPort;
};

} // namespace roadrunnerapi
