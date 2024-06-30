// #include "protogrpc/Util/Util.h"
#include "EventCalendar.h"
#include "EventPublisher.h"
#include "MessageLogger.h"
#include "ScenarioConfiguration.h"
#include "SimulationServer.h"
#include "Util.h"
#include "Utility.h"

#include <iostream>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/random_generator.hpp>

////////////////////////////////////////////////////////////////////////////////

// Convenience functions
namespace {
// Convenience functions
inline std::string GetNewQUuid() {
    return boost::lexical_cast<std::string>(boost::uuids::random_generator()());
}
} // namespace

////////////////////////////////////////////////////////////////////////////////

using namespace sseProto;

namespace sse {

EventPublisher::EventPublisher() {
    InitializeTimeoutValues();
    ReplayBehaviorUuid = GetNewQUuid();
}

////////////////////////////////////////////////////////////////////////////////

void EventPublisher::InitializeTimeoutValues() {
    // Get the timeout values from the scenario configuration data
    mDefaultTimeoutValues = sse::ScenarioConfiguration::GetTimeoutValues();

    // Keep the timeout values greater than or equal to the minimum timeout
    for (auto& it : mDefaultTimeoutValues) {
        if (MIN_TIMEOUT_IN_MS > it.second) {
            it.second = MIN_TIMEOUT_IN_MS;
        }
    }

    mTimeoutValues = mDefaultTimeoutValues;
}

////////////////////////////////////////////////////////////////////////////////

// locks: ClientAddRemoveMutex
// calls: NONE
void EventPublisher::AddSubscriber(const SubscribeEventsRequest& request,
                                   ::grpc::ServerContext*        ctx,
                                   EventWriter*                  writer) {
    auto id = request.client_id();

    {
        std::lock_guard lock(ClientAddRemoveMutex);

        // The client should have first registered with the server
        auto itr = IdToSubscribers.find(id);
        if (itr == IdToSubscribers.end()) {
            rrThrow("Client " + id + "has not registered. Register by calling 'RegisterClient'.");
        }

        // Verify the client is not already an event subscriber
        if (itr->second->mIsEventSubscriber) {
            rrThrow("Attempted to add subscriber with duplicate id : " + id);
        }

        // Validate client profile: an actor simulator client must use synchronous
        // communication
        const ClientProfile& clientProfile =
            request.client_profile();
        if (clientProfile.simulate_actors() && !clientProfile.synchronous()) {
            rrThrow("Invalid configuration for client '" + itr->second->mClientName + "' (id: " + id + "), a client must be synchronous for it to simulate actors.");
        }

        // Set the parameters for the subscriber
        itr->second->mContext           = ctx;
        itr->second->mWriter            = writer;
        itr->second->mProfile           = clientProfile;
        itr->second->mIsEventSubscriber = true;

        // Update timeout value if necessary
        ProcessProposedTimeout(itr->second.get());
    }

    {
        // Create & broadcast an event to notify that a new event subscriber was added
        auto evt = EventCalendar::CreateClientSubscribedEvent(id);
        BroadcastEvent(*evt);
    }
}

////////////////////////////////////////////////////////////////////////////////

// locks: ClientAddRemoveMutex, ClientsReadyMutex
// calls: NIL
void EventPublisher::RemoveSubscriber(const std::string& clientId) {
    // Remove the entry from the internal map
    RemoveSubscriberInternal(clientId, true);
}

////////////////////////////////////////////////////////////////////////////////

void EventPublisher::CancelSubscribers() {
    std::lock_guard lock(ClientAddRemoveMutex);

    while (!IdToSubscribers.empty()) {
        auto itr = IdToSubscribers.begin();
        // Create copy of the client id, since it will be removed from the container
        std::string clientID = itr->first;
        RemoveSubscriber(clientID);
    }
}

////////////////////////////////////////////////////////////////////////////////

void EventPublisher::SynchronousPublishAndWait(const Event& event) {
    std::string eventName = rrProtoGrpc::Util::GetOneofMessage(event,
                                                               *event.GetDescriptor()->FindOneofByName("type"))
                                .GetDescriptor()
                                ->name();

    // Get the event name and the timeout value for the event type
    // Default timeout value is the client proposed timeout value
    std::chrono::milliseconds timeOut(mMaxProposedTimeOut);
    {
        auto it = mTimeoutValues.find(eventName);
        if (it != mTimeoutValues.end() && it->second > mMaxProposedTimeOut) {
            timeOut = std::chrono::milliseconds(it->second);
        }
    }

    // Acquire lock
    std::unique_lock<std::mutex> clientReadyLock(ClientsReadyMutex);

    rrVerify(UnreadySynchronousClients.empty());

    // Set the status of all the clients as unready
    {
        std::lock_guard clientAddRemoveLock(ClientAddRemoveMutex);

        for (const auto& [clientId, subscriber] : IdToSubscribers) {
            if (subscriber->mIsEventSubscriber && subscriber->mProfile.synchronous()) {
                // UnreadySynchronousClients protected by unlock.
                UnreadySynchronousClients.insert(clientId);
            }
        }
    }

    // Publish event to all clients
    BroadcastEvent(event);

    // Relinquish lock & wait for acknowledgment from all synchronous clients
    do {
        NoHeartBeatSynchronousClients = UnreadySynchronousClients;

        bool ready = ClientsReadyConditionVariable.wait_for(
            clientReadyLock, timeOut, [&] { return UnreadySynchronousClients.empty() || NoHeartBeatSynchronousClients.empty(); });

        // If we have unready clients, time them out and print an error
        if (!ready) {
            // Remove the clients that failed to respond within the timeout
            // - NoHeartBeatSynchronousClients protected by ulock that is reacquired after wait_for returns.
            while (!NoHeartBeatSynchronousClients.empty()) {
                auto it       = NoHeartBeatSynchronousClients.begin();
                auto clientId = *it;
                NoHeartBeatSynchronousClients.erase(it);
                std::string clientName;
                if (IdToSubscribers.find(clientId) != IdToSubscribers.end()) {
                    clientName = IdToSubscribers[clientId]->mClientName;
                }

                RemoveSubscriberInternal(clientId, false);
                std::cout << "Error: Client '" << clientName << "' (id: " << clientId << ") timed out on event " << eventName << std::endl;
            }
        }
    } while (!UnreadySynchronousClients.empty());
}

////////////////////////////////////////////////////////////////////////////////

// locks: ClientAddRemoveMutex
// calls: NIL
void EventPublisher::BroadcastEvent(const ::Event& event) {
    std::lock_guard lock(ClientAddRemoveMutex);

    for (const auto& [clientId, subscriber] : IdToSubscribers) {
        // Write event if subscriber is an event subscriber
        if (subscriber->mIsEventSubscriber) {
            VZ_UNUSED(clientId);
            // Publish the event to the writer
            subscriber->mWriter->Write(event);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

bool EventPublisher::SetClientReady(const std::string& clientId) {
    bool allAlive = false;
    bool ready    = false;
    {
        std::lock_guard lock(ClientsReadyMutex);

        // Remove the client from the no-heart-beat client list
        NoHeartBeatSynchronousClients.erase(clientId);
        allAlive = NoHeartBeatSynchronousClients.empty();

        // Remove the client from the unready client list
        auto it = UnreadySynchronousClients.find(clientId);
        if (it != UnreadySynchronousClients.end()) {
            UnreadySynchronousClients.erase(it);
            ready = UnreadySynchronousClients.empty();
        } else {
            return false;
        }
    }

    // If all clients are ready, notify the event publish thread to continue
    if (allAlive || ready) {
        // There should only ever be one thread waiting on this cv (the main simulation thread).
        ClientsReadyConditionVariable.notify_one();
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////

void EventPublisher::SetClientBusy(const std::string& clientId, const std::string& status) {
    bool allAlive = false;
    {
        std::lock_guard lock(ClientsReadyMutex);

        // Remove the client from the unready client list
        auto it = NoHeartBeatSynchronousClients.find(clientId);
        if (it != NoHeartBeatSynchronousClients.end()) {
            NoHeartBeatSynchronousClients.erase(it);
            allAlive = NoHeartBeatSynchronousClients.empty();

            if (!status.empty()) {
                std::cout << "Info: Client " << clientId << ": " << status << std::endl;
            }
        }
    }

    // If all clients are alive, notify the event publish thread to continue to wait
    if (allAlive) {
        ClientsReadyConditionVariable.notify_one();
    }
}

////////////////////////////////////////////////////////////////////////////////

void EventPublisher::RemoveSubscriberInternal(const std::string& clientId, bool lockClientReadyMutex) {
    // Zero, If the client is not an event subscriber, remove it from the map and return
    {
        std::lock_guard lock(ClientAddRemoveMutex);

        auto itr = IdToSubscribers.find(clientId);
        if (itr != IdToSubscribers.end() && !itr->second->mIsEventSubscriber) {
            IdToSubscribers.erase(itr);
            return;
        }
    }

    // First resolve unready flag
    {
        bool notify = false;
        {
            std::unique_lock lock(ClientsReadyMutex, std::defer_lock);

            if (lockClientReadyMutex) {
                lock.lock();
            }

            // To make sure the subscriber is a synchronous subscriber.
            auto numRemoved = UnreadySynchronousClients.erase(clientId);
            if (numRemoved != 0) {
                notify = UnreadySynchronousClients.empty();
            }
        }

        if (notify) {
            ClientsReadyConditionVariable.notify_one();
        }
    }

    // Next remove subscriber
    {
        std::lock_guard lock(ClientAddRemoveMutex);

        auto iter = IdToSubscribers.find(clientId);
        if (iter == IdToSubscribers.end()) {
            return;
        }

        auto& subscriber = iter->second;

        // Trigger end of gRPC call
        subscriber->mContext->TryCancel();

        IdToSubscribers.erase(iter);

        // Update timeout value
        mTimeoutValues      = mDefaultTimeoutValues;
        mMaxProposedTimeOut = MIN_TIMEOUT_IN_MS;
        for (const auto& it : IdToSubscribers) {
            if (it.second->mIsEventSubscriber) {
                ProcessProposedTimeout(it.second.get());
            }
        }
    }

    // Last, send out unsubscribe event
    {
        auto evt = EventCalendar::CreateClientUnsubscribedEvent(clientId);
        BroadcastEvent(*evt);
    }
}

////////////////////////////////////////////////////////////////////////////////

void EventPublisher::ProcessProposedTimeout(const Subscriber* subscriber) {
    unsigned int proposed = subscriber->mProfile.timeout_milliseconds();
    // If proposed value is greater than the config value, update config value
    for (auto& it : mTimeoutValues) {
        if (proposed > it.second) {
            it.second = proposed;
        }
    }

    // If proposed value is greater than current maximum proposed value, update it
    if (proposed > mMaxProposedTimeOut) {
        mMaxProposedTimeOut = proposed;
    }
}

////////////////////////////////////////////////////////////////////////////////

std::string EventPublisher::AllocateActorSimulator(const Behavior& behavior, const std::string& actorId) {
    // If we are in replay mode and the actor is excluded, return the replay mode UUID
    if (IsReplayMode) {
        bool isBehaviorExcluded = ExcludedActorIds.find(std::stoull(actorId)) != ExcludedActorIds.end();
        if (!isBehaviorExcluded) {
            return ReplayBehaviorUuid;
        }
    }

    std::lock_guard guard(ClientAddRemoveMutex);

    int numSubscriberClients = 0;
    for (const auto& iter : IdToSubscribers) {
        // Skip non-event subscribers
        if (!iter.second->mIsEventSubscriber) {
            continue;
        }
        ++numSubscriberClients;
    }
    if (numSubscriberClients == 0) {
        // This is a special case that only possible in unit test
        return "";
    }

    std::string candidate;
    size_t      use = UINT_MAX;
    // First try to allocate a client that matches the actor behavior's requirements
    for (const auto& [id, subscriber] : IdToSubscribers) {
        // Skip non-event subscribers
        if (!subscriber->mIsEventSubscriber) {
            continue;
        }

        if (!subscriber->mProfile.simulate_actors()) {
            continue;
        }

        switch (subscriber->mProfile.platform_type_case()) {
        case ClientProfile::kRoadrunnerPlatform:
            if (behavior.has_roadrunner_behavior() && subscriber->mUse < use) {
                candidate = id;
                use       = subscriber->mUse;
            }
            break;
        case ClientProfile::kSimulinkPlatform:
            if (behavior.has_simulink_behavior() && subscriber->mUse < use) {
                candidate = id;
                use       = subscriber->mUse;
            }
            break;
        case ClientProfile::kExternalPlatform:
            if (behavior.has_external_behavior() &&
                (subscriber->mProfile.external_platform().platform_name() == behavior.external_behavior().platform_name()) &&
                subscriber->mUse < use) {
                candidate = id;
                use       = subscriber->mUse;
            }
            break;
        default:
            rrThrow("Unspecified platform type for client '" + subscriber->mClientName + "' (id: " + id + ").");
        }
    }
    if (candidate.size() > 0) {
        ++(IdToSubscribers[candidate]->mUse);
        return candidate;
    }

    // Backup plan: if no matching resource is found, try to simulate on a
    // RoadRunner simulation client.
    for (const auto& [id, subscriber] : IdToSubscribers) {
        // Skip non-event subscribers
        if (!subscriber->mIsEventSubscriber) {
            continue;
        }

        if (subscriber->mProfile.has_roadrunner_platform() &&
            subscriber->mProfile.simulate_actors() &&
            subscriber->mUse < use) {
            candidate = id;
            use       = subscriber->mUse;
        }
    }
    if (candidate.size() > 0) {
        std::ostringstream ss;
        ss << "Unable to locate a simulator for actor behavior '" << behavior.id() << "', ";
        ss << "applying RoadRunner default behavior to this actor." << std::endl;
        ss << "This is regarding to a behavior with the following attributes:" << std::endl;
        ss << "  - Behavior asset: " << behavior.asset_reference() << std::endl;
        switch (behavior.platform_type_case()) {
        case Behavior::kSimulinkBehavior:
            ss << "  - Platform type: MATLAB/Simulink" << std::endl;
            break;
        case Behavior::kExternalBehavior:
            ss << "  - Platform type: External" << std::endl;
            ss << "  - Platform name: " << behavior.external_behavior().platform_name() << std::endl;
            break;
        default:
            // The rest should already be handled (error out). Do nothing
            break;
        }
        ss << "The following simulation clients are currently available:" << std::endl;
        for (const auto& [id, subscriber] : IdToSubscribers) {
            // Skip non-event subscribers
            if (!subscriber->mIsEventSubscriber) {
                continue;
            }

            if (!subscriber->mProfile.simulate_actors()) {
                continue;
            }

            switch (subscriber->mProfile.platform_type_case()) {
            case ClientProfile::kRoadrunnerPlatform:
                ss << "  - " << id << ": "
                   << "RoadRunner" << std::endl;
                break;
            case ClientProfile::kSimulinkPlatform:
                ss << "  - " << id << ": "
                   << "MATLAB/Simulink" << std::endl;
                break;
            case ClientProfile::kExternalPlatform:
                ss << "  - " << id << ": "
                   << "External (" << subscriber->mProfile.external_platform().platform_name() << ")" << std::endl;
                break;
            default:
                //  The rest should already be handled (error out). Do nothing
                break;
            }
        }
        std::cout << "Warning: " << ss.str() << std::endl;
        ++(IdToSubscribers[candidate]->mUse);
        return candidate;
    }

    rrThrow("Fail to identify a simulator for actor behavior " + behavior.id());
}

////////////////////////////////////////////////////////////////////////////////

void EventPublisher::RegisterClient(const RegisterClientRequest* request, std::string& id) {
    std::unique_lock<std::recursive_mutex> lock(ClientAddRemoveMutex);

    auto itr = IdToSubscribers.find(id);
    if (itr != IdToSubscribers.end()) {
        rrThrow("Attempted to register client with duplicate id : " + id + ". Client '" + itr->second->mClientName + "' is currently registered with this id.");
    }

    rrUP<sse::EventPublisher::Subscriber> subscriber = std::make_unique<sse::EventPublisher::Subscriber>();
    subscriber->mId                                  = id;
    subscriber->mClientName                          = request->name();
    IdToSubscribers.insert({id, std::move(subscriber)});
}

////////////////////////////////////////////////////////////////////////////////

void SimulationEventPublisher::BroadcastEvent(const sseProto::Event& event) {

    // Log the broadcast message
    /*if (mSimulationServer &&
        mSimulationServer->IsLoggingEnabled() &&
        mMessageLogger)
    {
        mMessageLogger->AddPublishedMessage(event);
    }*/

    if (mSimulationServer && mRunTimeLogger &&
        mRunTimeLogger->IsRunTimeLoggingEnabled()) {
        mRunTimeLogger->LogSimulationEvent(event);
    }

    EventPublisher::BroadcastEvent(event);
}

////////////////////////////////////////////////////////////////////////////////
} // namespace sse
