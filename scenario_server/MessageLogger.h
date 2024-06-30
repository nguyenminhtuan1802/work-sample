#pragma once
// #include "scenariosimulationengine/Engine/Utility.h"
#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"

#include "RoadRunnerConversions.h"
#include "mathworks/scenario/simulation/cosim.grpc.pb.h"
#include "mathworks/scenario/simulation/logging.grpc.pb.h"
#include "mathworks/scenario/simulation/cosim_api.grpc.pb.h"

#include <mutex>
#include <deque>
#include <unordered_map>
#include <vector>
#include <string>

////////////////////////////////////////////////////////////////////////////////
// Class that holds a log of diagnostic information received by the gRPC server
// during simulation. One instance of this class is created when the gRPC server
// comes alive.
//
//	- thread-safe: All functions are thread-safe
//
namespace sse {
class Simulation;
class EventPublisher;
class Result;

class MessageLogger {
    VZ_DECLARE_NONCOPYABLE(MessageLogger);

  public:
    MessageLogger(Simulation* pSimulation);
    ~MessageLogger() = default;

    /*
     * Add an RPC message to the log
     * rpcName: The service name
     * clientID: the client id (uuid)
     * status: The return code of the rpc call
     */
    Result AddRPCMessage(const ::google::protobuf::Message& rpcName, const std::string& clientId, const Result& status);

    /*
     * Add an RPC message to the log
     * publicationName: The name of the published event
     */
    Result AddPublishedMessage(const sseProto::Event& event);

    /*
     * Retrieve a message log based on a query - invoked from 'SimulationServer' for the corresponding
     * RPC call
     */
    Result RetrieveMessages(const sseProto::QueryCommunicationLogRequest& request,
                            sseProto::QueryCommunicationLogResponse&      response);

    /*
     * Clears the cached messages
     */
    void ClearMessages();

    /*
     * Add client id of an event subscriber - so we know when the client started
     * receiving broadcast events.
     */
    void RegisterEventSubscriberID(std::string& id);

    /*
     * Remove client id of an event subscriber - so we know when the client stopped
     * receiving broadcast events.
     */
    void UnregisterEventSubscriberID(std::string& id);

    /*
     * Set the buffer size limit on the communication log. The length can be expanded but not
     * contracted. Returns true if successfully modified the limit else false
     */
    Result SetBufferLength(const sseProto::EnableCommunicationLoggingRequest& request, sseProto::EnableCommunicationLoggingResponse& response);

  private:
    ////////////////////////////////////////////////////////////////////////////////

    /*
     * Add a communication message (both rpc and published messages) to the log
     */
    Result AddMessage(sseProto::Communication& message, bool isSimOverEvt = false);

    /*
     * Returns the name of the event (the 'oneof' type)
     * (Refer to SimulationClient.h for details)
     */
    std::string GetEventName(const sseProto::Event& event);

    /*
     * Returns a set of the indices into 'CommunicationLog' corresponding to a given client id.
     * The set includes rpc calls from the client and the published messages to the client.
     */
    std::set<size_t> GetLogIndicesForClientId(std::string& clientId);

    /*
     * For the popped out messages, remove the corresponding indices from the index deques
     */
    void CullInvalidIndices();

    /*
     * For the popped out messages, remove the corresponding indices from the index deques
     */
    void EraseNegativeIndices(std::deque<size_t>& q);

  private:
    std::mutex mMessageAccessMutex;

    // A deque holding rpc or published messages - new messages are pushed back.
    // When the deque reaches the length limit, messages are popped from the front.
    std::deque<sseProto::Communication> mCommunicationLog;

    // A map of clientID vs an array of indices for the client ID
    std::unordered_map<std::string, std::deque<size_t>> mClientIDRPCMessageIndex;

    // A map of service type Vs an array of indices for service type
    std::unordered_map<std::string, std::deque<size_t>> mRPCTypeMessageIndex;

    // A deque of published message indices
    std::deque<size_t> mPublishMessageIndices;

    // A map of published message type Vs an array of indices into for published message
    std::unordered_map<std::string, std::deque<size_t>> mPublishTypeMessageIndex;

    // Simulation Times Vs the log indices (RPC and published messages)
    std::deque<std::pair<double, size_t>> mSimulationTimeIndex;

    // Client id's of clients Vs starting index when the client subscribed to events
    // and ending index of when the client unsubscribed from events.
    std::unordered_map<std::string, std::pair<size_t, size_t>> mClientIDPublishIndices;

    Simulation* mSimulation = nullptr; // To retrieve simulation time

    // Message logging buffer length (default is 0: unlimited)
    std::atomic<unsigned int> mBufferLength = 0;

    // counter holding the number of messages that were pushed back
    size_t mNumPushedBack = 0;

    // counter holding the number of messages that were popped front
    size_t mNumPoppedFront = 0;

    double mCachedSimTime = 0.0;
};

class ActorRunTimeLogger {
    VZ_DECLARE_NONCOPYABLE(ActorRunTimeLogger);

  public:
    ActorRunTimeLogger(Simulation* pSimulation);
    ~ActorRunTimeLogger() = default;

    /*
     * Log simulation event to the simulation log
     * Result: The return code
     */
    Result LogSimulationEvent(const sseProto::Event& evt);

    /*
     * Log actor states to the log
     * Result: The return code
     */
    Result LogActorRunTimeStates(const std::unordered_map<std::string, rrSP<sseProto::ActorRuntime>>& actorStates);

    /*
     * Retrieve a message log based on a query - invoked from 'ScenarioServer' for the corresponding
     * RPC call
     */
    Result RetrieveMessages(const sseProto::QueryWorldRuntimeLogRequest& request,
                            sseProto::QueryWorldRuntimeLogResponse&      response);

    /*
     * Clears the cached messages
     */
    void ClearMessages();

    /*
     * Set the buffer size limit on the communication log. The length can be expanded but not
     * contracted. Returns true if successfully modified the limit else false
     */
    Result EnableActorRuntimeLogging(const sseProto::EnableWorldRuntimeLoggingRequest& request, sseProto::EnableWorldRuntimeLoggingResponse&);

    /*
     * Returns bool, indicating if runtime logging is enabled or not
     */
    bool IsRunTimeLoggingEnabled() {
        return mEnableRuntimeLogging;
    }

    /*
     * Updates the simulation settings to the log, to be called before simulation start
     */
    void UpdateSimulationSettings();

  private:
    // Removes invalid indices
    void CullInvalidIndices();

    Result UpdateWorldState(const std::function<void(sseProto::WorldRuntimeState& worldState)>& updationFunc);

  private:
    std::mutex mDataAccessMutex;

    // A deque holding the world runtime state.
    std::deque<sseProto::WorldRuntimeState> mWorldRuntimeStateLog;

    // A map of actor id vs a deque of index into 'mWorldRuntimeStateLog' plus corresponding entry for actor
    std::unordered_map<std::string, std::deque<std::pair<size_t, size_t>>> mActorIDWorldStateIndex;

    // Simulation Times Vs the log indices
    std::deque<std::pair<double, size_t>> mSimulationTimeIndex;

    Simulation* mSimulation = nullptr; // To retrieve simulation time

    // Message logging buffer length (default is 0: unlimited)
    std::atomic<unsigned int> mBufferLength = 0;

    std::atomic_bool mEnableRuntimeLogging = false;

    // counter holding the number of messages that were pushed back
    size_t mNumPushedBack = 0;

    // counter holding the number of messages that were popped front
    size_t mNumPoppedFront = 0;

    // Simulation settings for the logged simulation run
    sseProto::SimulationSettings mSimulationSettings;
};

class DiagnosticMessageLogger {
    VZ_DECLARE_NONCOPYABLE(DiagnosticMessageLogger);

  public:
    DiagnosticMessageLogger(Simulation* pSimulation);
    ~DiagnosticMessageLogger() = default;

    /*
     * Add a diagnostic message to the log
     * Result: The return code
     */
    Result AddDiagnosticMessages(const sseProto::AddDiagnosticMessageRequest& request, sseProto::AddDiagnosticMessageResponse&);

    /*
     * Retrieve a message log based on a query - invoked from 'SimulationServer' for the corresponding
     * RPC call
     */
    Result RetrieveMessages(const sseProto::QueryDiagnosticMessageLogRequest& request,
                            sseProto::QueryDiagnosticMessageLogResponse&      response);

    /*
     * Clears the cached messages
     */
    void ClearMessages();

  private:
    std::mutex mDataAccessMutex;

    // A deque holding the diagnostic messages.
    std::vector<sseProto::DiagnosticMessage> mDiagnosticMessageLog;

    // A map of clientID vs indices into 'mDiagnosticMessageLog'
    std::unordered_map<std::string, std::vector<size_t>> mClientIDMsgIndex;

    // A map of diagnostic message type vs indices into 'mDiagnosticMessageLog'
    std::unordered_map<sseProto::DiagnosticType, std::vector<size_t>> mMsgTypeMsgIndex;

    // Simulation Times Vs the log indices
    std::deque<std::pair<double, size_t>> mSimulationTimeIndex;

    Simulation* mSimulation = nullptr; // To retrieve simulation time
};
} // namespace sse
