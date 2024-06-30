#include "Parameter.h"

namespace roadrunner::proto {

////////////////////////////////////////////////////////////////////////////////

Parameter::Parameter(const sseProto::Attribute& parameter)
    : mParameter(parameter) {
}

////////////////////////////////////////////////////////////////////////////////

Parameter::Parameter(const Parameter& parameter)
    : mParameter(parameter.mParameter) {
}

////////////////////////////////////////////////////////////////////////////////

const std::string& Parameter::GetParameterNameFromProto() const {
    return mParameter.data().struct_element().elements(0).array_element().elements(0).string_element();
}

////////////////////////////////////////////////////////////////////////////////

const sseProtoCommon::Value& Parameter::GetParameterValueFromProto() const {
    return mParameter.data().struct_element().elements(1).array_element().elements(0);
}

////////////////////////////////////////////////////////////////////////////////

EnumParameterDataType Parameter::GetParameterDataTypeFromProto() const {
    const auto& value = GetParameterValueFromProto();

    switch (value.type_case()) {
    case sseProtoCommon::Value::kNumberElement: {
        switch (value.number_element().type_case()) {
        case sseProtoCommon::Number::kDoubleElement:
            return EnumParameterDataType::eDouble;
        case sseProtoCommon::Number::kUint16Element:
            return EnumParameterDataType::eUint16;
        case sseProtoCommon::Number::kInt32Element:
            return EnumParameterDataType::eInt32;
        case sseProtoCommon::Number::kUint32Element:
            return EnumParameterDataType::eUint32;
        default:
            rrThrow("Unsupported parameter data type.");
        }
        break;
    }
    case sseProtoCommon::Value::kLogicalElement:
        return EnumParameterDataType::eBoolean;
    case sseProtoCommon::Value::kStringElement:
        return EnumParameterDataType::eString;
    case sseProtoCommon::Value::kCharElement:
        return EnumParameterDataType::eString;
    default:
        rrThrow("Unsupported parameter data type.");
        break;
    }
}

////////////////////////////////////////////////////////////////////////////////

void Parameter::ValidateParameterStructure() const {
    // Scenario Parameter structure in proto format:
    // - Attribute
    //		- Struct
    //			- Field(0): "Name"
    //			- Element(0): Array
    //							- Value(0): "MyParam"
    //			- Field(1): "Value"
    //			- Element(1): Array
    //							- Value(0): "MyParamValue" or Number or Logical...
    //
    // See array.proto

    rrThrowVerify(
        mParameter.has_data(), "Parameter " + mParameter.name() + " has invalid data model. Parameter must have \"data\" field.");
    // Verify struct size
    rrThrowVerify(
        mParameter.data().has_struct_element() &&
            mParameter.data().struct_element().elements_size() == 2,
        "Parameter " + mParameter.name() + " has invalid data model. Parameter must have \"struct\" field.");
    // Verify struct's field values
    rrThrowVerify(
        mParameter.data().struct_element().names(0) == "Name" &&
            mParameter.data().struct_element().names(1) == "Value",
        "Parameter " + mParameter.name() + " has invalid data model. Parameter's struct field must have \"Name\" and \"Value\" fields.");
    // Verify struct's type
    rrThrowVerify(
        mParameter.data().struct_element().elements(0).has_array_element() &&
            mParameter.data().struct_element().elements(1).has_array_element(),
        "Parameter " + mParameter.name() + " has invalid data model. Parameter's struct must have \"array\" field.");
    // Verify struct's element size
    rrThrowVerify(
        mParameter.data().struct_element().elements(0).array_element().elements_size() == 1 &&
            mParameter.data().struct_element().elements(1).array_element().elements_size() == 1,
        "Parameter " + mParameter.name() + " has invalid data model. Parameter's array must have size 1.");
    // Verify parameter name value is not empty
    rrThrowVerify(
        !mParameter.data().struct_element().elements(0).array_element().elements(0).string_element().empty(), "Parameter " + mParameter.name() + " has invalid data model. Parameter must have \"data\" field");
}

////////////////////////////////////////////////////////////////////////////////

} // namespace roadrunner::proto
