#pragma once

// #include "Parameter.h"
#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "RoadRunnerConversions.h"
#include "Parameter.h"

#include "mathworks/scenario/simulation/custom_command.pb.h"

namespace roadrunner::proto {

////////////////////////////////////////////////////////////////////////////////
// Wrapper class for custom event type
//
class CustomEventDefinition {
  public:
    CustomEventDefinition() = default;
    CustomEventDefinition(const sseProto::CustomCommand& event);

    void Initialize();

    void ValidateEvent(const sseProto::CustomCommand& event);

  private:
    sseProto::CustomCommand                                                   mEvent;
    std::unordered_map<std::string, roadrunner::proto::EnumParameterDataType> mParameterDataTypeMap;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace roadrunner::proto
