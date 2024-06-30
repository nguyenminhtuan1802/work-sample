// Copyright 2022 The MathWorks, Inc.
#pragma once

#include "mw_grpc_clients/mw_grpc_clients_spec.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

namespace mathworks { namespace sysinterface {
class Args;
class ComponentInterface;
}} // namespace mathworks::sysinterface

namespace sysinterf_grpc {

struct SysApiGrpcContext;

class MW_GRPC_CLIENTS_EXPORT_CLASS SysApiGrpcClient final {
  public:
    SysApiGrpcClient(const std::string& address);
    ~SysApiGrpcClient();

    // returns the unique client id for this client
    std::string_view getId() const;

    // returns if the gRPC client is shutdown
    bool isConnected() const;

    // send a request to server to setup simulation
    mathworks::sysinterface::Args setupSimulation(const mathworks::sysinterface::Args& req);

    // send a request to server to cleanup simulation
    mathworks::sysinterface::Args cleanupSimulation(const mathworks::sysinterface::Args& req);

    // send a request to server to start simulation
    mathworks::sysinterface::Args startSimulation(const mathworks::sysinterface::Args& req);

    // send a request to server to stop simulation
    mathworks::sysinterface::Args stopSimulation(const mathworks::sysinterface::Args& req);

    // send a request to server to step simulation
    mathworks::sysinterface::Args output(const mathworks::sysinterface::Args& req);

    // invoke a service at server port
    mathworks::sysinterface::Args callFunction(const mathworks::sysinterface::Args& req);

    // send a request to server to update status
    mathworks::sysinterface::Args update(const mathworks::sysinterface::Args& req);

    // send a request to server to get ComponentInterface
    mathworks::sysinterface::ComponentInterface getComponentInterface(const mathworks::sysinterface::Args& req);

    // returns the current state of all the actors
    mathworks::sysinterface::Args listActor(const mathworks::sysinterface::Args& req);

    // send the input port values to an actor
    mathworks::sysinterface::Args setInputs(const mathworks::sysinterface::Args& req);

    // set the parameter values for an actor
    mathworks::sysinterface::Args setParams(const mathworks::sysinterface::Args& req);

    // get the parameter values for an actor
    mathworks::sysinterface::Args getParams(const mathworks::sysinterface::Args& req);

    // get the default values for an actor
    mathworks::sysinterface::Args getDefaultValues(const mathworks::sysinterface::Args& req);

  private:
    // connects to the server at the given address and returns status
    void connect(const std::string& address);
    // shut down the gRPC client
    void disconnect();

    std::unique_ptr<SysApiGrpcContext> mGrpcContext;

    // client Id
    std::string mId;

    // flag indicates whether this client is shutting down
    std::atomic<bool> mIsConnected;
};

} // namespace sysinterf_grpc
