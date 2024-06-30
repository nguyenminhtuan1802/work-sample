#pragma once

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "RoadRunnerConversions.h"

#include "mathworks/scenario/simulation/attributes.pb.h"

namespace roadrunner::proto {

////////////////////////////////////////////////////////////////////////////////

enum class EnumParameterDataType : int {
    eDouble,
    eUint16,
    eInt32,
    eUint32,
    eBoolean,
    eDateTime,
    eString,
    eInvalid
};

////////////////////////////////////////////////////////////////////////////////
// Wrapper class for parameter
//
class Parameter {
  public:
    Parameter() = default;
    Parameter(const sseProto::Attribute& parameter);
    Parameter(const Parameter& parameter);

    // *IMPORTANT* Validate parameter structure first before using the helping getters
    void                         ValidateParameterStructure() const;
    const std::string&           GetParameterNameFromProto() const;
    const sseProtoCommon::Value& GetParameterValueFromProto() const;
    EnumParameterDataType        GetParameterDataTypeFromProto() const;

  private:
    sseProto::Attribute mParameter;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace roadrunner::proto

////////////////////////////////////////////////////////////////////////////////
