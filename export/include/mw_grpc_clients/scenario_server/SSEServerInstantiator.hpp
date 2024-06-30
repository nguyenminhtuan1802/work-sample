#pragma once

#include "mw_grpc_clients/mw_grpc_clients_spec.hpp"
#include "ProtobufNamespace.hpp"

#include <memory>
#include <string>

namespace sse {
class ScenarioExecutor;
class Simulation;
class SimulationServer;

////////////////////////////////////////////////////////////////////////////////
// Entry point class to instantiate the scenario server
class MW_GRPC_CLIENTS_EXPORT_CLASS SSEServerInstantiator {
  public:
    SSEServerInstantiator();
    virtual ~SSEServerInstantiator();

    bool StartScenarioServer(std::string& address, int port);
    void StopScenarioServer();

    bool isServerInstantiated() {
        return (mSSEServer != nullptr);
    }

    virtual std::unique_ptr<ScenarioExecutor> CreateActivityDiagramExecutor(
        const sseProto::Actor& worldActor,
        Simulation*            sim) = 0;

  private:
    std::unique_ptr<SimulationServer> mSSEServer;
};

} // namespace sse
