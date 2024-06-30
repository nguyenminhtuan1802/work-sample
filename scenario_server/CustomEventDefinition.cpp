#include "CustomEventDefinition.h"

namespace roadrunner::proto {

////////////////////////////////////////////////////////////////////////////////

CustomEventDefinition::CustomEventDefinition(const sseProto::CustomCommand& event)
    : mEvent(event) {
    Initialize();
}

////////////////////////////////////////////////////////////////////////////////

void CustomEventDefinition::Initialize() {
    for (int i = 0; i < mEvent.attributes_size(); ++i) {
        roadrunner::proto::Parameter param{mEvent.attributes(i)};

        param.ValidateParameterStructure();

        auto name = param.GetParameterNameFromProto();
        auto type = param.GetParameterDataTypeFromProto();
        mParameterDataTypeMap.insert({name, type});
    }
}

////////////////////////////////////////////////////////////////////////////////

void CustomEventDefinition::ValidateEvent(const sseProto::CustomCommand& event) {
    rrThrowVerify(mEvent.name() == event.name(), "Undefined event name " + event.name());

    for (int i = 0; i < event.attributes_size(); ++i) {
        roadrunner::proto::Parameter param{mEvent.attributes(i)};

        param.ValidateParameterStructure();

        auto name = param.GetParameterNameFromProto();
        auto type = param.GetParameterDataTypeFromProto();

        rrThrowVerify(mParameterDataTypeMap.find(name) != mParameterDataTypeMap.end(), "Undefined parameter name: " + name);
        rrThrowVerify(mParameterDataTypeMap[name] == type, "Unmatched parameter data type for parameter: " + name);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace roadrunner::proto
