#include "mw_grpc_clients/scenario_server/SSEServerInstantiator.hpp"

#include "SimulationServer.h"

namespace sse {
////////////////////////////////////////////////////////////////////////////////

SSEServerInstantiator::SSEServerInstantiator() {}

////////////////////////////////////////////////////////////////////////////////

SSEServerInstantiator::~SSEServerInstantiator() = default;

////////////////////////////////////////////////////////////////////////////////

bool SSEServerInstantiator::StartScenarioServer(std::string& address, int port) {
    mSSEServer = std::make_unique<SimulationServer>(address, port);
    mSSEServer->SetInstantiator(this);
    return true;
}

////////////////////////////////////////////////////////////////////////////////

void SSEServerInstantiator::StopScenarioServer() {
    mSSEServer.reset();
}
} // namespace sse
