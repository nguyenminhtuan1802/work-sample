#pragma once

#include "Simulation.h"
#include "RoadRunnerConversions.h"

#include "mathworks/scenario/simulation/actor.pb.h"
#include "mathworks/scenario/simulation/action.pb.h"
#include "mathworks/scenario/simulation/condition.pb.h"
#include "mathworks/scenario/simulation/scenario.pb.h"

namespace sse {

////////////////////////////////////////////////////////////////////////////
// Helper class used by scenario engine classes to indicate results of a
// service call. In the case that the service call is achieve via gRPC,
// a Result object will be converted into ::grpc::status, thus a gRPC client
// can extract such service result together with other possible service
// call status.
class Result {
  public:
    enum class EnumType {
        eSuccess = 0,
        eError   = 1
    };

    Result() = default;

    Result(const std::string& msg)
        : Type(EnumType::eError)
        , Message(msg) {}

    static Result Error(const std::string& msg) {
        return Result(msg);
    }

    static Result Success() {
        return Result();
    }

    void AddError(const std::string& msg) {
        Type = EnumType::eError;
        Message += msg + "\n";
    }

    EnumType    Type = EnumType::eSuccess;
    std::string Message;
};

////////////////////////////////////////////////////////////////////////////
//
class ScenarioSuccessException : public rrException {
  public:
    ScenarioSuccessException()
        : rrException("No message")
        , mHasMessage(false) {}

    sseProto::SuccessStatus mSuccessStatus;
    std::string             mSummary;

  protected:
    std::string mMessage;
    bool        mHasMessage = false;
};

////////////////////////////////////////////////////////////////////////////
//
class ScenarioFailureException : public rrException {
  public:
    ScenarioFailureException()
        : rrException("No message")
        , mHasMessage(false) {}

    sseProto::FailureStatus mFailureStatus;
    std::string             mSummary;

  protected:
    std::string mMessage;
    bool        mHasMessage = false;
};

////////////////////////////////////////////////////////////////////////////
//
class StringConversionException : public rrException {};
} // namespace sse
