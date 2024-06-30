/* Copyright 2021-2023 The MathWorks, Inc.

RoadRunnerConnectivity calls the RoadRunner Connectivity API using the gRPC stub.

*/
#pragma once

#include "mw_grpc_clients/mw_grpc_clients_spec.hpp"

#include <memory>

namespace mathworks::roadrunner {
class TestConnectionRequest;
class TestConnectionResponse;
} // namespace mathworks::roadrunner

namespace rrProto = mathworks::roadrunner;

namespace roadrunnerapi {

struct RoadRunnerConnectivityGrpcContext;

class MW_GRPC_CLIENTS_EXPORT_CLASS RoadRunnerConnectivity final {
  public:
    RoadRunnerConnectivity();
    ~RoadRunnerConnectivity();

    void Connect(int apiPort = 35708);

    double GetTestConnectionPort() const {
        return mTestConnectionPort;
    };

    bool IsServerReady() const;

    // Call a RoadRunnerService method
    void TestConnection(const rrProto::TestConnectionRequest* request) const;

  private:
    std::unique_ptr<RoadRunnerConnectivityGrpcContext> mGrpcContext;
    int                                                mTestConnectionPort;
};

} // namespace roadrunnerapi
