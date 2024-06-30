#pragma once

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "RoadRunnerConversions.h"

#include "mathworks/scenario/simulation/behavior.pb.h"
#include "mathworks/scenario/simulation/event.pb.h"
#include "mathworks/scenario/simulation/cosim_api.grpc.pb.h"

#include <condition_variable>
#include <string>

////////////////////////////////////////////////////////////////////////////////
// Class that holds a list of connected gRPC clients and publishes signals to.
// One instance of this class is created and connected/disconnected clients
// are added/removed from it.
//
//	- thread-safe: All functions are thread-safe
//
namespace sse {

class MessageLogger;
class SimulationServer;
class ActorRunTimeLogger;

class EventPublisher {
    VZ_DECLARE_NONCOPYABLE(EventPublisher);

  public:
    EventPublisher();

    virtual ~EventPublisher() {}

    using EventWriter = ::grpc::ServerWriter<sseProto::Event>;

    //
    // Add a client to publish events to
    //
    void AddSubscriber(const sseProto::SubscribeEventsRequest& request, ::grpc::ServerContext* ctx, EventWriter* writer);

    // Removes a subscriber.
    // - The simulation will be unblocked if it is waiting for this subscriber.
    void RemoveSubscriber(const std::string& clientId);

    // Cancel all subscribers
    void CancelSubscribers();

    //
    // Publish an event to all synchronous clients and wait until SynchronousClientsContinue()
    // has been invoked subsequently.
    //
    void SynchronousPublishAndWait(const sseProto::Event& event);

    // Broadcast an event to all subscribers
    virtual void BroadcastEvent(const sseProto::Event& event);

    //
    // Notify when a client is ready for the next event. Returns 'true' if client has subscribed
    // for events. Returns 'false' if client has not subscribed for events OR if client already
    // indicated its status as ready.
    //
    bool SetClientReady(const std::string& clientId);

    // Process a notification from a client that the client is alive but busy
    void SetClientBusy(const std::string& clientId, const std::string& status);

    // Find a client that can simulate the specified actor behavior
    // - Returns a client id
    // - Throw an error if no such a client is available
    std::string AllocateActorSimulator(const sseProto::Behavior& behavior, const std::string& actorId);

    void RegisterClient(const sseProto::RegisterClientRequest* request, std::string& id);

    // Encapsulates event subscriber attributes.
    class Subscriber {
      public:
        std::string            mId;
        EventWriter*           mWriter  = nullptr;
        ::grpc::ServerContext* mContext = nullptr;
        // Outline of the client's properties and capabilities
        sseProto::ClientProfile mProfile;
        // Number of actors currently simulated by this client
        size_t mUse = 0;
        // Name of the client
        std::string mClientName;
        // Whether the client is an event subscriber
        bool mIsEventSubscriber = false;
    };

    // Return a map of current subscriber id to its profile;
    const std::unordered_map<std::string, rrUP<Subscriber>>* const GetClientIdToSubscriberMap() const {
        return &IdToSubscribers;
    }

    std::recursive_mutex& GetClientAddRemoveMutex() {
        return ClientAddRemoveMutex;
    }

    void SetReplayMode(bool isReplayMode, const google::protobuf::RepeatedPtrField<std::string>& excludedIds) {
        IsReplayMode = isReplayMode;
        ExcludedActorIds.clear();
        for (const auto& id : excludedIds) {
            unsigned long long actorId = std::stoull(id);
            // rrPrint("Actor id to exclude: %1 "_Q % actorId);
            ExcludedActorIds.insert(actorId);
        }
    }

  private:
    // Initialize the timeout values from scenario configuration data
    void InitializeTimeoutValues();

    // Manage mutex state carefully
    // - lockClientReadyMutex should be set to true if the ClientReadyMutex is not locked externally
    void RemoveSubscriberInternal(const std::string& clientId, bool lockClientReadyMutex);

    // Process timeout value proposed by a subscriber
    void ProcessProposedTimeout(const Subscriber* subscriber);

  private:
    // Wait time of server for client acknowledgment in seconds
    static const unsigned int MIN_TIMEOUT_IN_MS = 2000;

    // Default timeout values from xml are cached - so timeout values
    // can be reset to default whenever a client leaves.
    std::unordered_map<std::string, unsigned int> mDefaultTimeoutValues;

    // Timeout values that are used during simulation, based on client negotiation
    std::unordered_map<std::string, unsigned int> mTimeoutValues;

    // Clients can propose a timeout value, we use the max proposed value.
    // We cache the max proposed value so that it can be used as the default
    unsigned int mMaxProposedTimeOut = MIN_TIMEOUT_IN_MS;

    // Mutex to protect adding and removing subscribers.
    std::recursive_mutex                              ClientAddRemoveMutex;
    std::unordered_map<std::string, rrUP<Subscriber>> IdToSubscribers;

    // Mutex to protect unready list of subscriber ids.
    mutable std::mutex              ClientsReadyMutex;
    std::unordered_set<std::string> UnreadySynchronousClients;
    std::unordered_set<std::string> NoHeartBeatSynchronousClients;
    std::condition_variable         ClientsReadyConditionVariable;
    std::string                     ReplayBehaviorUuid;
    std::atomic<bool>               IsReplayMode = false;
    std::set<unsigned long long>    ExcludedActorIds;
};

class SimulationEventPublisher : public EventPublisher {
    VZ_DECLARE_NONCOPYABLE(SimulationEventPublisher);

  public:
    SimulationEventPublisher(SimulationServer* server)
        : EventPublisher()
        , mSimulationServer(server) {}

    virtual ~SimulationEventPublisher() {}

    SimulationServer* GetSimulationServer() const {
        return mSimulationServer;
    }

    void BroadcastEvent(const sseProto::Event& event) override;

    void SetMessageLogger(MessageLogger* pLogger) {
        mMessageLogger = pLogger;
    }

    void SetRuntimeLogger(ActorRunTimeLogger* pRtLogger) {
        mRunTimeLogger = pRtLogger;
    }

  private:
    SimulationServer*   mSimulationServer = nullptr;
    MessageLogger*      mMessageLogger    = nullptr;
    ActorRunTimeLogger* mRunTimeLogger    = nullptr;
};
} // namespace sse
