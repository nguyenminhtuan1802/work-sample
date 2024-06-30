#pragma once

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "Phase.h"

#include "mathworks/scenario/simulation/condition.pb.h"

namespace sse {
namespace conversion {

////////////////////////////////////////////////////////////////////////////////

template <typename EnumT>
VZ_NO_RETURN void ReportUnexpectedEnumValue(EnumT val) {
    rrThrow("Unexpected enum value: " + std::to_string(static_cast<int>(val)));
}

}} // namespace sse::conversion

////////////////////////////////////////////////////////////////////////////////

mathworks::scenario::simulation::PhaseState sseConvert(const sse::Phase::State& from) {
    using namespace sse;
    switch (from) {
    case Phase::State::eIdle:
        return sseProto::PhaseState::PHASE_STATE_IDLE;
    case Phase::State::eStart:
        return sseProto::PhaseState::PHASE_STATE_START;
    case Phase::State::eRun:
        return sseProto::PhaseState::PHASE_STATE_RUN;
    case Phase::State::eEnd:
        return sseProto::PhaseState::PHASE_STATE_END;
    default:
        conversion::ReportUnexpectedEnumValue<Phase::State>(from);
    }
}

////////////////////////////////////////////////////////////////////////////////

mathworks::scenario::common::Vector3 sseConvert(const std::array<double, 3>& fromPoint) {
    mathworks::scenario::common::Vector3 rtPoint;

    rtPoint.set_x(fromPoint[0]);
    rtPoint.set_y(fromPoint[1]);
    rtPoint.set_z(fromPoint[2]);

    return rtPoint;
}

////////////////////////////////////////////////////////////////////////////////
