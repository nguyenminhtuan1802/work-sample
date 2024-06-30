
#include "MessageLogger.h"
#include "Simulation.h"
#include "EventPublisher.h"
#include "Utility.h"
#include "Util.h"

#include <google/protobuf/util/time_util.h>

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

////////////////////////////////////////////////////////////////////////////////

namespace {

////////////////////////////////////////////////////////////////////////////////
// Get the current simulation time (returns DBL_MIN if simulation has not started
// or Simulation is nullptr)
double GetCurrentSimTime(sse::Simulation* pSimulation) {
    if (pSimulation) {
        return pSimulation->GetTimeNow();
    }

    return DBL_MIN;
}

////////////////////////////////////////////////////////////////////////////////

// Removes any invalid (negative) values from a deque having <time, index> pairs
void CullSimTimeIndices(std::deque<std::pair<double, size_t>>& timeIndices, size_t numPopped) {
    for (auto& elem : timeIndices) {
        if ((int)(elem.second - numPopped) < 0) {
            timeIndices.pop_front();
        } else {
            break;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

// Returns the start and end indices from time index data, given the start and end simulation time
// [TODO]: Have to consider negative start time too
std::pair<int, int> GetIndicesForTime(double startSimTime, double endSimTime, std::deque<std::pair<double, size_t>>& simTimeIdx, int numPopped) {
    // If there are no messages, negative time ranges, return out of range => {-1, -1}
    if (simTimeIdx.size() == 0 || startSimTime < 0.0 || endSimTime < 0.0 || endSimTime < startSimTime) {
        return {-1, -1};
    }

    // By default we return everything.
    // Since at the call site, we iterate in the range [startIdx, endIdx), we are setting 'endIdx' to last element idx + 1
    int startIdx((int)simTimeIdx.front().second - numPopped);
    int endIdx((int)simTimeIdx.back().second - numPopped + 1);

    // If the start and end times are in the current range, search for the index
    if (startSimTime <= simTimeIdx.back().first && startSimTime >= simTimeIdx.front().first) {
        // Find the index from where the start time begins
        auto it = std::lower_bound(simTimeIdx.begin(), simTimeIdx.end(), startSimTime, [](const auto& f, double value) {
            return f.first < value;
        });

        if (it != simTimeIdx.end()) {
            startIdx = (int)it->second - numPopped;
        }
    }

    if (endSimTime <= simTimeIdx.back().first && endSimTime >= simTimeIdx.front().first) {
        // Find the last index where the end time ends
        auto it = std::upper_bound(simTimeIdx.begin(), simTimeIdx.end(), endSimTime, [](double value, const auto& f) {
            return value < f.first;
        });

        if (it != simTimeIdx.end()) {
            endIdx = (int)it->second - numPopped;
        }
    }

    // If start and end times are out of current index range, return out of range => {-1, -1}
    if (endSimTime < simTimeIdx.front().first || startSimTime > simTimeIdx.back().first) {
        return {-1, -1};
    }

    return {startIdx, endIdx};
}

////////////////////////////////////////////////////////////////////////////////

double SetTimeStamp(sseProto::TimeStamp* timeStamp, sse::Simulation* pSimulation, bool skipTimeStamp = false, double useSimTime = 0.0) {
    // Get current simulation time, if it is not already passed in
    double currentSimTime = 0.0;
    skipTimeStamp ? currentSimTime = useSimTime : currentSimTime = ::GetCurrentSimTime(pSimulation);

    // Get current wall clock time
    const auto                timenow{std::chrono::system_clock::now()};
    std::chrono::microseconds msec = std::chrono::duration_cast<std::chrono::microseconds>(timenow.time_since_epoch());

    timeStamp->set_simulation_time(currentSimTime);
    auto currentWCTime = google::protobuf::util::TimeUtil::MicrosecondsToTimestamp(msec.count());
    auto wcTime        = timeStamp->mutable_wall_clock_time();
    wcTime->set_seconds(currentWCTime.seconds());
    wcTime->set_nanos(currentWCTime.nanos());

    return currentSimTime;
}

////////////////////////////////////////////////////////////////////////////////
} // namespace

namespace sse {

////////////////////////////////////////////////////////////////////////////////
//					MessageLogger
////////////////////////////////////////////////////////////////////////////////

Result MessageLogger::AddRPCMessage(const ::google::protobuf::Message& request, const std::string& clientId, const Result& status) {
    sseProto::Communication        message;
    sseProto::RemoteProcedureCall* rpcMsg = message.mutable_rpc_call();
    rpcMsg->set_client_id(clientId);
    rpcMsg->set_service_name(request.GetDescriptor()->name());
    rpcMsg->set_success(status.Type == Result::EnumType::eSuccess);

    AddMessage(message);

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result MessageLogger::AddPublishedMessage(const sseProto::Event& event) {
    sseProto::Communication   message;
    sseProto::PublishMessage* publishMsg = message.mutable_published_message();
    publishMsg->set_message_type(GetEventName(event));

    // When 'SimulationOver' event is published, simulation isn't running anymore. So current
    // simulation time is undefined. But the default value for double in protobuf is 0.0
    // and that is throwing off the binary search in the log query (the log is not sorted
    // by time anymore). So currently we show the simulation end time as the simulation
    // time in case of 'SimulationOver' event.
    AddMessage(message, event.type_case() == sseProto::Event::kSimulationStatusChangedEvent);

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result MessageLogger::RetrieveMessages(const sseProto::QueryCommunicationLogRequest& request,
                                       sseProto::QueryCommunicationLogResponse&      response) {
    // Check if start and end times were specified. Get the values
    double startSimTime(-1.0), endSimTime(-1.0);
    if (request.has_start_time()) {
        startSimTime = request.start_time().simulation_time();
    }
    if (request.has_end_time()) {
        endSimTime = request.end_time().simulation_time();
    }

    // In case no criteria is specified, we return all the messages (rpc, publish)
    bool returnAllMessages(true);

    // Create the response
    {
        // Acquire (Shared?) lock to read messages
        std::lock_guard  lock(mMessageAccessMutex);
        std::set<size_t> resultIdxSet;

        // If the client ID was provided, include the rpc & published messages corresponding to it
        std::string clientId = request.client_id();
        if (clientId != "") {
            returnAllMessages = false; // query not empty, need not return all messages
            resultIdxSet      = GetLogIndicesForClientId(clientId);
        }

        // If rpc type is not empty, add corresponding rpc messages to the response
        std::string rpcType = request.rpc_type();
        if (rpcType != "") {
            returnAllMessages = false;

            auto itr = mRPCTypeMessageIndex.find(rpcType);
            if (itr != mRPCTypeMessageIndex.end()) {
                std::for_each(itr->second.begin(), itr->second.end(), [&resultIdxSet, this](const size_t& idx) {
                    resultIdxSet.insert(idx - mNumPoppedFront);
                });
            }
        }

        // If published message type is not empty, add corresponding published messages to the response
        std::string publishType = request.publish_type();
        if (publishType != "") {
            returnAllMessages = false;

            auto itr = mPublishTypeMessageIndex.find(publishType);
            if (itr != mPublishTypeMessageIndex.end()) {
                std::for_each(itr->second.begin(), itr->second.end(), [&resultIdxSet, this](const size_t& idx) {
                    resultIdxSet.insert(idx - mNumPoppedFront);
                });
            }
        }

        // Get the time based message set and do an intersection with the current result set
        if (startSimTime >= 0.0 || endSimTime >= 0.0) {
            returnAllMessages = false; // criteria provided, need not return all messages

            std::set<size_t> timeIdxSet;
            auto             range = ::GetIndicesForTime(startSimTime, endSimTime, mSimulationTimeIndex, (int)mNumPoppedFront);
            for (size_t idx = range.first; idx < static_cast<size_t>(range.second); ++idx) {
                timeIdxSet.insert(idx);
            }

            // If current result set isn't empty, intersect with the time set
            if (resultIdxSet.size() > 0) {
                std::set<size_t> tmpSet;
                std::set_intersection(timeIdxSet.begin(), timeIdxSet.end(), resultIdxSet.begin(), resultIdxSet.end(),
                                      std::inserter(tmpSet, tmpSet.end()));
                resultIdxSet = tmpSet;
            } else {
                resultIdxSet = timeIdxSet;
            }
        }

        // Iterate over the result set and create response
        if (resultIdxSet.size() > 0) {
            for (size_t idx : resultIdxSet) {
                response.mutable_communication_log()->mutable_communication_log()->Add()->CopyFrom(mCommunicationLog[idx]);
            }
        }

        // In case the query was empty, return all the messages in the response
        if (returnAllMessages) {
            for (auto& msg : mCommunicationLog) {
                response.mutable_communication_log()->mutable_communication_log()->Add()->CopyFrom(msg);
            }
        }
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

void MessageLogger::ClearMessages() {
    // TODO: figure out the appropriate place from where we might be able to call this method
    // or from where this component can be re-instantiated.
    // Acquire (exclusive?) the lock to update message data
    std::lock_guard lock(mMessageAccessMutex);

    // Clear all the containers
    mCommunicationLog.clear();
    mClientIDRPCMessageIndex.clear();
    mRPCTypeMessageIndex.clear();
    mPublishTypeMessageIndex.clear();
    mPublishMessageIndices.clear();
    mSimulationTimeIndex.clear();
    mClientIDPublishIndices.clear();
    mNumPushedBack  = 0;
    mNumPoppedFront = 0;
}

////////////////////////////////////////////////////////////////////////////////

MessageLogger::MessageLogger(Simulation* pSimulation)
    : mSimulation(pSimulation) {
}

////////////////////////////////////////////////////////////////////////////////

void MessageLogger::RegisterEventSubscriberID(std::string& id) {
    // Acquire (exclusive?) lock to write data
    std::lock_guard lock(mMessageAccessMutex);

    // If the client has not already subscribed, add client id and log index
    auto iter = mClientIDPublishIndices.find(id);
    if (iter == mClientIDPublishIndices.end()) {
        mClientIDPublishIndices[id] = std::make_pair(mNumPushedBack, std::numeric_limits<size_t>::max());
    }
}

////////////////////////////////////////////////////////////////////////////////

void MessageLogger::UnregisterEventSubscriberID(std::string& id) {
    // Acquire (exclusive?) lock to write data
    std::lock_guard lock(mMessageAccessMutex);

    // The client should have already subscribed, add end log index
    auto iter = mClientIDPublishIndices.find(id);
    if (iter != mClientIDPublishIndices.end()) {
        iter->second.second = mNumPushedBack;
    }
}

////////////////////////////////////////////////////////////////////////////////

Result MessageLogger::SetBufferLength(const sseProto::EnableCommunicationLoggingRequest& request, sseProto::EnableCommunicationLoggingResponse&) {
    mBufferLength = request.log_buffer_length();
    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result MessageLogger::AddMessage(sseProto::Communication& message, bool isSimOverEvent) {
    // Set the timestamp on the message
    sseProto::TimeStamp* timeStamp = message.mutable_time_stamp();

    // Set time stamp
    double currentSimTime = mCachedSimTime = ::SetTimeStamp(timeStamp, mSimulation, isSimOverEvent, mCachedSimTime);

    // Acquire (exclusive?) lock to update message data, indices etc.
    std::lock_guard lock(mMessageAccessMutex);

    // Store the message
    mCommunicationLog.emplace_back(message);
    ++mNumPushedBack;

    // If the buffer length limit has been reached, pop-out a message from the front
    if (mBufferLength != 0 && mCommunicationLog.size() > mBufferLength) {
        mCommunicationLog.pop_front();
        ++mNumPoppedFront;
        CullInvalidIndices();
    }

    if (message.has_rpc_call()) {
        auto&       rpc         = message.rpc_call();
        std::string clientID    = rpc.client_id();
        std::string serviceName = rpc.service_name();

        // Add indices for fast searching based on client id
        mClientIDRPCMessageIndex[clientID].emplace_back(mNumPushedBack - 1);

        // Add indices for fast searching based on service name
        mRPCTypeMessageIndex[serviceName].emplace_back(mNumPushedBack - 1);
    } else // Published message type
    {
        auto&       publishedMessage = message.published_message();
        std::string messageType      = publishedMessage.message_type();

        // Add index of published message
        mPublishMessageIndices.emplace_back(mNumPushedBack - 1);

        // Add indices for fast searching based on published message type
        mPublishTypeMessageIndex[messageType].emplace_back(mNumPushedBack - 1);
    }

    // Update the simulation time index
    mSimulationTimeIndex.emplace_back(std::pair(currentSimTime, mNumPushedBack - 1));

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

std::string MessageLogger::GetEventName(const sseProto::Event& event) {
    try {
        return rrProtoGrpc::Util::GetOneofMessage(event,
                                                  *event.GetDescriptor()->FindOneofByName("type"))
            .GetDescriptor()
            ->name();
    } catch (const rrException& /*e*/) {
        return "UnknownEvent";
    }
}

////////////////////////////////////////////////////////////////////////////////

std::set<size_t> MessageLogger::GetLogIndicesForClientId(std::string& clientId) {
    std::set<size_t> resultIdxSet;
    auto             rpcItr = mClientIDRPCMessageIndex.find(clientId);
    if (rpcItr != mClientIDRPCMessageIndex.end()) {
        std::for_each(rpcItr->second.begin(), rpcItr->second.end(), [&resultIdxSet, this](const size_t& idx) {
            resultIdxSet.insert(idx - mNumPoppedFront);
        });
    }

    // If the client id is an event subscriber, then return relevant published messages too
    auto pubItr = mClientIDPublishIndices.find(clientId);
    if (pubItr != mClientIDPublishIndices.end()) {
        // In case the end is equal to numeric max, reset it to current log size
        size_t endIndex = pubItr->second.second - mNumPoppedFront;
        if (endIndex > mCommunicationLog.size()) {
            endIndex = mCommunicationLog.size();
        }

        // Add the indices to result during which the client was an event subscriber
        for (size_t i : mPublishMessageIndices) {
            if (i >= pubItr->second.first && i < endIndex) {
                resultIdxSet.insert(i - mNumPoppedFront);
            }

            if (i >= endIndex) {
                break;
            }
        }
    }

    return resultIdxSet;
}

////////////////////////////////////////////////////////////////////////////////

void MessageLogger::CullInvalidIndices() {
    for (auto& elem : mClientIDRPCMessageIndex) {
        EraseNegativeIndices(elem.second);
    }

    for (auto& elem : mRPCTypeMessageIndex) {
        EraseNegativeIndices(elem.second);
    }

    for (auto& elem : mPublishTypeMessageIndex) {
        EraseNegativeIndices(elem.second);
    }

    ::CullSimTimeIndices(mSimulationTimeIndex, mNumPoppedFront);

    auto itr = mClientIDPublishIndices.begin();
    while (itr != mClientIDPublishIndices.end()) {
        if ((int)(itr->second.second - mNumPoppedFront) < 0) {
            itr = mClientIDPublishIndices.erase(itr);
        } else {
            if ((int)(itr->second.first - mNumPoppedFront) < 0) {
                itr->second.first = mNumPoppedFront;
            }

            ++itr;
        }
    }

    EraseNegativeIndices(mPublishMessageIndices);
}

////////////////////////////////////////////////////////////////////////////////

void MessageLogger::EraseNegativeIndices(std::deque<size_t>& q) {
    for (auto& idx : q) {
        if ((int)(idx - mNumPoppedFront) < 0) {
            q.pop_front();
        } else {
            break;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
//					ActorRunTimeLogger
////////////////////////////////////////////////////////////////////////////////

ActorRunTimeLogger::ActorRunTimeLogger(Simulation* pSimulation)
    : mSimulation(pSimulation) {
    // QObject::connect(pSimulation, &Simulation::SimulationStarted, this, &ActorRunTimeLogger::ClearMessages);
}

////////////////////////////////////////////////////////////////////////////////

Result ActorRunTimeLogger::UpdateWorldState(const std::function<void(sseProto::WorldRuntimeState& worldState)>& updationFunc) {
    // If runtime logging is not enabled, we issue an error
    if (!mEnableRuntimeLogging) {
        return Result::Error("WorldRuntimeLogging logging has not been enabled.");
    }

    // Get current simulation time
    double currentSimTime = ::GetCurrentSimTime(mSimulation);
    {
        // Acquire the lock to update data
        std::lock_guard lock(mDataAccessMutex);

        bool createNewWorldState(true);
        // If we have runtime states in the log, check to see if we already
        // have a state for the current time-step. Refer to: AddActorRunTimeStates()
        if (mWorldRuntimeStateLog.size() > 0) {
            auto&  worldRtState = mWorldRuntimeStateLog.back();
            double rtSimTime    = worldRtState.time_stamp().simulation_time();
            // If we already have a world state for the current time, update it
            // to add custom commands
            if (EpsilonEqual(rtSimTime, currentSimTime)) {
                createNewWorldState = false;
            }
        }

        if (createNewWorldState) {
            // Create and add commands to it
            sseProto::WorldRuntimeState worldState;
            updationFunc(worldState);

            // Set the time stamp on the world state
            sseProto::TimeStamp* timeStamp = worldState.mutable_time_stamp();

            // Set time stamp
            ::SetTimeStamp(timeStamp, mSimulation, true, currentSimTime);

            // Add the world state to the runtime log
            mWorldRuntimeStateLog.emplace_back(worldState);
            ++mNumPushedBack;

            // If the buffer length limit has been reached, pop-out a message from the front
            if (mBufferLength != 0 && mWorldRuntimeStateLog.size() > mBufferLength) {
                mWorldRuntimeStateLog.pop_front();
                ++mNumPoppedFront;
                CullInvalidIndices();
            }

            // Add an index for the time
            mSimulationTimeIndex.emplace_back(std::pair(currentSimTime, mNumPushedBack - 1));
        } else {
            // Add actor commands to the existing state
            auto& worldRtState = mWorldRuntimeStateLog.back();
            updationFunc(worldRtState);
        }
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result ActorRunTimeLogger::LogSimulationEvent(const sseProto::Event& evt) {
    // Early return for trivial events, such as step, postStep, stop etc.
    if (!(evt.has_action_event() || evt.has_action_complete_event() ||
          evt.has_user_defined_event())) {
        return Result::Success();
    }

    // Update the world state with the event
    auto res = UpdateWorldState(
        [&evt](sseProto::WorldRuntimeState& worldState) {
            sseProto::Event* event = worldState.add_simulation_events();
            event->CopyFrom(evt);
        });

    return res;
}

////////////////////////////////////////////////////////////////////////////////

Result ActorRunTimeLogger::LogActorRunTimeStates(const std::unordered_map<std::string, rrSP<sseProto::ActorRuntime>>& actorStates) {
    // Method to add actor runtime states
    auto res = UpdateWorldState(
        [&actorStates, this](sseProto::WorldRuntimeState& worldState) {
            for (const auto& [id, actorState] : actorStates) {
                sseProto::ActorRuntime* actorRt = worldState.add_actor_runtimes();
                actorRt->CopyFrom(*actorState);
                mActorIDWorldStateIndex[id].emplace_back(std::make_pair(mNumPushedBack, (size_t)(worldState.actor_runtimes_size() - 1)));
            }
        });

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result ActorRunTimeLogger::RetrieveMessages(const sseProto::QueryWorldRuntimeLogRequest& request,
                                            sseProto::QueryWorldRuntimeLogResponse&      response) {
    if (!mEnableRuntimeLogging) {
        return Result::Error("WorldRuntimeLogging logging has not been enabled.");
    }

    // Acquire the lock to retrieve data
    std::lock_guard lock(mDataAccessMutex);

    // Set the simulation settings from the log
    *(response.mutable_world_runtime_state_log()->mutable_simulation_settings()) = mSimulationSettings;

    // Get the indices for given start and end time from the request.
    // Check if start and end times were specified. Get the values.
    double startSimTime(-1.0), endSimTime(-1.0);
    if (request.has_start_time()) {
        startSimTime = request.start_time().simulation_time();
    }
    if (request.has_end_time()) {
        endSimTime = request.end_time().simulation_time();
    }

    bool returnAllMessages(true);

    auto range        = ::GetIndicesForTime(startSimTime, endSimTime, mSimulationTimeIndex, (int)mNumPoppedFront);
    int  startTimeIdx = range.first;
    int  endTimeIdx   = range.second;

    std::string actorId = request.actor_id();
    if (actorId != "") // If client id was supplied
    {
        auto itr = mActorIDWorldStateIndex.find(actorId);
        if (itr != mActorIDWorldStateIndex.end()) {
            for (auto& [logIdx, clientIdx] : itr->second) {
                if (static_cast<size_t>(startTimeIdx) <= (logIdx - mNumPoppedFront) && (logIdx - mNumPoppedFront) <= static_cast<size_t>(endTimeIdx)) {
                    // Get the particular state for the given client id
                    sseProto::WorldRuntimeState worldState = mWorldRuntimeStateLog[logIdx - mNumPoppedFront];
                    auto                        actorState = worldState.actor_runtimes(clientIdx);

                    // Copy the time and actor state to the response
                    sseProto::WorldRuntimeState* worldRtState = response.mutable_world_runtime_state_log()->add_world_states();
                    worldRtState->mutable_time_stamp()->CopyFrom(worldState.time_stamp());
                    sseProto::ActorRuntime* actRt = worldRtState->add_actor_runtimes();
                    actRt->CopyFrom(actorState);
                }
            }
        }

        returnAllMessages = false;
    } else if (startTimeIdx >= 0 && endTimeIdx >= 0) // Return the response if only time limits provided or empty request
    {
        for (size_t idx = startTimeIdx; idx < static_cast<size_t>(endTimeIdx); ++idx) {
            // Copy the world state to the response
            sseProto::WorldRuntimeState  worldState   = mWorldRuntimeStateLog[idx];
            sseProto::WorldRuntimeState* worldRtState = response.mutable_world_runtime_state_log()->add_world_states();
            worldRtState->CopyFrom(worldState);
        }

        returnAllMessages = false;
    }

    if (returnAllMessages) {
        // In case the query was empty, return all the messages in the response
        for (const auto& rtState : mWorldRuntimeStateLog) {
            sseProto::WorldRuntimeState* worldRtState = response.mutable_world_runtime_state_log()->add_world_states();
            worldRtState->CopyFrom(rtState);
        }
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

void ActorRunTimeLogger::ClearMessages() {
    // This method is called at the start of simulation,
    // clear cache
    std::lock_guard lock(mDataAccessMutex);
    mWorldRuntimeStateLog.clear();
    mActorIDWorldStateIndex.clear();
    mSimulationTimeIndex.clear();

    mNumPushedBack  = 0;
    mNumPoppedFront = 0;

    // Add the simulation settings to the log
    UpdateSimulationSettings();
}

////////////////////////////////////////////////////////////////////////////////

void ActorRunTimeLogger::CullInvalidIndices() {
    // Cull 'mSimulationTimeIndex'
    ::CullSimTimeIndices(mSimulationTimeIndex, mNumPoppedFront);

    // Cull 'mActorIDWorldStateIndex'
    for (auto& clientIdState : mActorIDWorldStateIndex) {
        auto stateIndices = clientIdState.second;
        for (auto& state : stateIndices) {
            if ((int)(state.first - mNumPoppedFront) < 0) {
                stateIndices.pop_front();
            } else {
                break;
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

Result ActorRunTimeLogger::EnableActorRuntimeLogging(const sseProto::EnableWorldRuntimeLoggingRequest& request, sseProto::EnableWorldRuntimeLoggingResponse&) {
    mBufferLength         = request.buffer_length();
    mEnableRuntimeLogging = request.log_world_runtime();

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

void ActorRunTimeLogger::UpdateSimulationSettings() {
    if (mSimulation) {
        sseProto::GetSimulationSettingsRequest  req;
        sseProto::GetSimulationSettingsResponse res;
        mSimulation->GetSimulationSettings(req, res);
        mSimulationSettings = res.simulation_settings();
    }
}

////////////////////////////////////////////////////////////////////////////////
//					DiagnosticMessageLogger
////////////////////////////////////////////////////////////////////////////////

DiagnosticMessageLogger::DiagnosticMessageLogger(Simulation* pSimulation)
    : mSimulation(pSimulation) {
    // QObject::connect(pSimulation, &Simulation::SimulationStarted, this, &DiagnosticMessageLogger::ClearMessages);
}

////////////////////////////////////////////////////////////////////////////////

Result DiagnosticMessageLogger::AddDiagnosticMessages(const sseProto::AddDiagnosticMessageRequest& request, sseProto::AddDiagnosticMessageResponse&) {
    // Add the messages to the log
    for (int ii = 0; ii < request.diagnostic_messages_size(); ++ii) {
        sseProto::DiagnosticMessage message  = request.diagnostic_messages(ii);
        std::string                 clientId = message.client_id();
        auto                        msgType  = message.diagnostic_type();
        message.clear_time_stamp();

        mClientIDMsgIndex[clientId].emplace_back(mDiagnosticMessageLog.size());
        mMsgTypeMsgIndex[msgType].emplace_back(mDiagnosticMessageLog.size());

        // Overwrite the time-stamp
        sseProto::TimeStamp* timeStamp      = message.mutable_time_stamp();
        double               currentSimTime = ::SetTimeStamp(timeStamp, mSimulation);

        mSimulationTimeIndex.emplace_back(std::pair(currentSimTime, mDiagnosticMessageLog.size()));

        mDiagnosticMessageLog.emplace_back(message);
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

void DiagnosticMessageLogger::ClearMessages() {
    std::lock_guard lock(mDataAccessMutex);

    mDiagnosticMessageLog.clear();
    mClientIDMsgIndex.clear();
    mMsgTypeMsgIndex.clear();
    mSimulationTimeIndex.clear();
}

////////////////////////////////////////////////////////////////////////////////

Result DiagnosticMessageLogger::RetrieveMessages(const sseProto::QueryDiagnosticMessageLogRequest& request,
                                                 sseProto::QueryDiagnosticMessageLogResponse&      response) {
    auto clientId = request.client_id();
    auto msgType  = request.diagnostic_type();

    // Check if start and end times were specified. Get the values
    double startSimTime(-1.0), endSimTime(-1.0);
    if (request.has_start_time()) {
        startSimTime = request.start_time().simulation_time();
    }
    if (request.has_end_time()) {
        endSimTime = request.end_time().simulation_time();
    }

    bool returnAllMessages(true);

    // Fill the set of indices to return for time range
    std::set<size_t> resultIdxSet;
    if (startSimTime >= 0.0 || endSimTime >= 0.0) {
        auto range = ::GetIndicesForTime(startSimTime, endSimTime, mSimulationTimeIndex, 0);
        for (size_t idx = range.first; idx < static_cast<size_t>(range.second); ++idx) {
            resultIdxSet.insert(idx);
        }
        returnAllMessages = false;
    }

    if (clientId != "") {
        // Find the indices corresponding to the client id
        std::set<size_t> clientIDIdxSet;
        auto             itr = mClientIDMsgIndex.find(clientId);
        if (itr != mClientIDMsgIndex.end()) {
            std::for_each(itr->second.begin(), itr->second.end(), [&clientIDIdxSet](const size_t& idx) {
                clientIDIdxSet.insert(idx);
            });
        }

        // Intersect if time filter applied
        if (!returnAllMessages) {
            std::set<size_t> tmpSet;
            std::set_intersection(clientIDIdxSet.begin(), clientIDIdxSet.end(), resultIdxSet.begin(), resultIdxSet.end(),
                                  std::inserter(tmpSet, tmpSet.end()));
            resultIdxSet = tmpSet; // update result
        } else {
            resultIdxSet = clientIDIdxSet;
        }

        returnAllMessages = false; // query not empty, need not return all messages
    }


    if (msgType != sseProto::DiagnosticType::UNKNOWN_TYPE) {
        // retrieve all the messages corresponding to message type
        std::set<size_t> msgTypeIdxSet;
        auto             itr = mMsgTypeMsgIndex.find(msgType);
        if (itr != mMsgTypeMsgIndex.end()) {
            std::for_each(itr->second.begin(), itr->second.end(), [&msgTypeIdxSet](const size_t& idx) {
                msgTypeIdxSet.insert(idx);
            });
        }

        // Intersect with result
        if (!returnAllMessages) {
            std::set<size_t> tmpSet;
            std::set_intersection(msgTypeIdxSet.begin(), msgTypeIdxSet.end(), resultIdxSet.begin(), resultIdxSet.end(),
                                  std::inserter(tmpSet, tmpSet.end()));
            resultIdxSet = tmpSet; // update result
        } else {
            resultIdxSet = msgTypeIdxSet;
        }

        returnAllMessages = false; // query not empty, need not return all messages
    }

    // Iterate over the result set and create response
    if (resultIdxSet.size() > 0) {
        for (size_t idx : resultIdxSet) {
            response.mutable_diagnostic_message_log()->add_diagnostic_messages()->CopyFrom(mDiagnosticMessageLog[idx]);
        }
    }

    // In case the query was empty, return all the messages in the response
    if (returnAllMessages) {
        for (auto& msg : mDiagnosticMessageLog) {
            response.mutable_diagnostic_message_log()->add_diagnostic_messages()->CopyFrom(msg);
        }
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////
} // namespace sse

////////////////////////////////////////////////////////////////////////////////
