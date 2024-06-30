#pragma once

#include "RoadRunnerConversions.h"
#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"

#include "mathworks/scenario/simulation/event.pb.h"

#include <limits>
#include <set>

namespace sse {

// Constant time tolerance value
static const double cTimeTolerance = 64 * DBL_EPSILON;

////////////////////////////////////////////////////////////////////////////////
// TimedEvent defines a time-indexed event.
// - It tracks a few scheduling related information related to a proto Event
//   object. Currently only including event time and event priority
// - In the future, when RaiseEvent service is provided to clients (e.g. via
//   gRPC service), we might need to add additional information: such as
//   client Id and event scheduling order.
class TimedEvent {
  public:
    TimedEvent(double time, const rrSP<sseProto::Event>& evt)
        : Time(time)
        , RtEvent(evt) {}

    // Operator
    bool operator<(const TimedEvent& other) const {
        if (Time < (other.Time - cTimeTolerance)) {
            return true;
        } else if (Time > (other.Time + cTimeTolerance)) {
            return false;
        }
        return (RtEvent->priority() < other.RtEvent->priority());
    }

    bool operator<=(double t) const {
        return (Time <= (t + cTimeTolerance));
    }

    // Event time
    double Time = 0;

    // Event
    rrSP<sseProto::Event> RtEvent;
};

////////////////////////////////////////////////////////////////////////////
// An EventCalendar maintains a set of to-be-processed events. The events
// are ordered with their { time, priority } values.
class EventCalendar {
  public:
    void SetTimeNow(double timeNow) {
        TimeNow = timeNow;
    }

    double GetTimeNow() const {
        return TimeNow;
    }

    double AdvanceTime() {
        if (Events.empty()) {
            TimeNow = DBL_MAX;
            return 0.0;
        } else {
            double prevTime = TimeNow;
            TimeNow         = Events.begin()->Time;
            return (TimeNow - prevTime); // Return the time that will elapse from the previous step
        }
    }

    void Cleanup() {
        Events.clear();
        TimeNow = 0.0;
    }

    void ScheduleEvent(double eventTime, const rrSP<sseProto::Event>& evt) {
        rrVerify(eventTime > TimeNow - cTimeTolerance);
        TimedEvent timedEvent(eventTime, evt);
        Events.insert(timedEvent);
    }

    rrSP<sseProto::Event> PopNextEvent() {
        rrSP<sseProto::Event> evt;
        if (!Events.empty()) {
            std::multiset<TimedEvent>::const_iterator it = Events.begin();
            evt                                          = it->RtEvent;
            Events.erase(it);
        }
        return evt;
    }

    bool HasPresentEvent() const {
        bool ret = false;
        if (!Events.empty()) {
            ret = (*Events.begin() <= TimeNow);
        }
        return ret;
    }

  private:
    std::atomic<double>       TimeNow = 0.0;
    std::multiset<TimedEvent> Events;

  public:
    // Static utility functions

    // Create events for client-server management
    static rrSP<sseProto::Event> CreateServerShutdownEvent();
    static rrSP<sseProto::Event> CreateClientSubscribedEvent(const std::string& client_id);
    static rrSP<sseProto::Event> CreateClientUnsubscribedEvent(const std::string& client_id);

    // Create events for notifying changes in scene, scenario, simulation
    // settings, and simulation runtime status
    static rrSP<sseProto::Event> CreateSceneChangedEvent();
    static rrSP<sseProto::Event> CreateScenarioChangedEvent();
    static rrSP<sseProto::Event> CreateSimulationSettingsChangedEvent(const sseProto::SimulationSettings& settings);
    static rrSP<sseProto::Event> CreateSimulationStatusChangedEvent(sseProto::SimulationStatus status);

    // Create co-simulation events
    static rrSP<sseProto::Event> CreateSimulationStartEvent(bool isReplayMode = false);
    static rrSP<sseProto::Event> CreateSimulationStopEvent(double eventT, int numSteps, const sseProto::SimulationStopCause& cause);
    static rrSP<sseProto::Event> CreateSimulationStepEvent(double eventT, int numSteps);
    static rrSP<sseProto::Event> CreateSimulationPostStepEvent();
    static rrSP<sseProto::Event> CreateScenarioUpdateEvent();

    // Create scenario-logic events
    static rrSP<sseProto::Event> CreateCreateActorEvent(const sseProto::Actor&                    actor,
                                                        const std::vector<rrSP<sseProto::Actor>>& descendants,
                                                        sseProto::Phase*                          initPhase = nullptr);
    static rrSP<sseProto::Event> CreateActionEvent(const sseProto::Phase& phase);
    static rrSP<sseProto::Event> CreateActionEvent(const google::protobuf::mathworks::RepeatedPtrField<sseProto::Action>& actions);

    static rrSP<sseProto::Event> CreateActionCompleteEvent(const std::string&          actionId,
                                                           const std::string&          actorId,
                                                           sseProto::ActionEventStatus finalStatus);

    // Pre-defined event priority for events.
    static const int cEventPriorityClientServer       = -2;
    static const int cEventPriorityNotifyChanges      = -1;
    static const int cEventPrioritySimulationStart    = 0;
    static const int cEventPrioritySimulationStop     = INT_MAX;
    static const int cEventPrioritySimulationStep     = 1000;
    static const int cEventPrioritySimulationPostStep = 2000;
    static const int cEventPriorityScenarioUpdate     = 1500;
    static const int cEventPriorityScenarioLogic      = 100;

    friend class SimulationTestFixture;
};
} // namespace sse
