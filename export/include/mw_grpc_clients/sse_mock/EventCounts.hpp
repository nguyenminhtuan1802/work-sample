#pragma once

#include "mw_grpc_clients/mw_grpc_clients_spec.hpp"

namespace sse_mock {
struct EventCounts {
    int RegisterClientHandled;
    int setReadyHandled;
    int SubscribeEventHandled;
    int UploadScenarioHandled;
    int DownloadScenarioHandled;
    int GetSimulationSettingsHandled;
    int StopSimulationHandled;
    int StopSimulationRequestedHandled;
    int SetRuntimeActorsHandled;
    int GetRuntimeActorsHandled;
    int GetActorsHandled;
};
} // namespace sse_mock

////////////////////////////////////////////////////////////////////////////////
