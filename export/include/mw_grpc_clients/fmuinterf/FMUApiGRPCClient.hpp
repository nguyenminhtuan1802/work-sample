// Copyright 2022 The MathWorks, Inc.
#pragma once
#include "mw_grpc_clients/mw_grpc_clients_spec.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <functional>

namespace mathworks { namespace sysinterface {
class Args;
class ComponentInterface;
}} // namespace mathworks::sysinterface

namespace sysinterf_grpc {

struct FMUApiGrpcContext;

class MW_GRPC_CLIENTS_EXPORT_CLASS FMUApiGRPCClient final {
  public:
    FMUApiGRPCClient(const std::string& address);
    ~FMUApiGRPCClient();

    // returns the unique client id for this client
    std::string_view getId() const;

    // returns if the gRPC client is shutdown
    bool isConnected() const;

    void shutdown() const;

    void setIsConnected(bool);

    // RPC method
    mathworks::sysinterface::Args LoadBinary(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args UnloadBinary(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetTypesPlatform(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetVersion(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args Instantiate(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args SetupExperiment(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args EnterInitializationMode(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args ExitInitializationMode(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args Terminate(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args Reset(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args FreeInstance(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args SetDebugLogging(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args SetReal(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args SetInteger(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args SetBoolean(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args SetString(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetReal(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetInteger(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetBoolean(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetString(const mathworks::sysinterface::Args& req);

    // Model Exchange Functions
    mathworks::sysinterface::Args SetTime(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args SetContinuousStates(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args EnterEventMode(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args NewDiscreteStates(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args EnterContinuousTimeMode(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args CompletedIntegratorStep(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetDerivatives(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetEventIndicators(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetContinuousStates(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetNominalsOfContinuousStates(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args SetRealInputDerivatives(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetRealOutputDerivatives(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args CancelStep(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetStatus(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetRealStatus(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetIntegerStatus(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetBooleanStatus(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetStringStatus(const mathworks::sysinterface::Args& req);

    // Optional Functions
    mathworks::sysinterface::Args SerializedFMUstateSize(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args SerializeFMUstate(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args DeSerializeFMUstate(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetFMUstate(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args SetFMUstate(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args FreeFMUstate(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args GetDirectionalDerivative(const mathworks::sysinterface::Args& req);
    mathworks::sysinterface::Args DoStep(const mathworks::sysinterface::Args& req);
    void                          ShutdownService(const mathworks::sysinterface::Args& req);

  private:
    // connects to the server at the given address and returns status
    void connect(const std::string& address);
    // shut down the gRPC client
    void disconnect();

    std::unique_ptr<FMUApiGrpcContext> mGrpcContext;

    // client Id
    std::string mId;

    // flag indicates whether this client is shutting down
    std::atomic<bool> mIsConnected;
};

} // namespace sysinterf_grpc
