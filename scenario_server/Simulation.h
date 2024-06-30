#pragma once

/*#include "core/Core/Memory.h"
#include "core/Core/ProtobufInclude.h"
#include "core/Core/Core.h"
#include "core/Licensing/LicenseMgr.h"
#include "proto/Util/ScenarioNamespace.h"
*/

#include "EventCalendar.h"
#include "CustomEventDefinition.h"

#include "mathworks/scenario/simulation/actor.pb.h"
#include "mathworks/scenario/simulation/cosim.pb.h"

#include <boost/lexical_cast.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <future>
#include <shared_mutex>
#include <unordered_map>

// VZ_FORWARD_DECLARE(animation, class CharacterAsset);
// VZ_FORWARD_DECLARE(animation, class CharacterRuntime);

namespace sse {

class ActorRunTimeLogger;
class BootstrapManager;
class MapService;
class Phase;
class Result;
class ScenarioExecutor;
class SimulationEventPublisher;
class ReplayBehavior;
class DiagnosticMessageLogger;
class SSEServerInstantiator;

////////////////////////////////////////////////////////////////////////////
// Class Simulation represents a scenario simulation run. It is created
// upon the start of a simulation and destroyed upon stop of a simulation.
class Simulation /*: public QObject*/
{
    VZ_DECLARE_NONCOPYABLE(Simulation);

  public:
    // Constructor and destructor
    Simulation(SimulationEventPublisher* publisher);
    ~Simulation();

    // Implementations of grpc service calls

    // Services for map retrieval
    Result UploadMap(const sseProto::UploadMapRequest& req, sseProto::UploadMapResponse& res);
    Result DownloadMap(const sseProto::DownloadMapRequest& req, sseProto::DownloadMapResponse& res) const;

    // Services for scenario configuration
    Result UploadScenario(const sseProto::UploadScenarioRequest& req, sseProto::UploadScenarioResponse& res);
    Result DownloadScenario(const sseProto::DownloadScenarioRequest& req, sseProto::DownloadScenarioResponse& res) const;

    // Simulation configuration
    Result SetSimulationSettings(const sseProto::SetSimulationSettingsRequest& req, sseProto::SetSimulationSettingsResponse& res);
    Result SetSimulationPace(const sseProto::SetSimulationPaceRequest& req, sseProto::SetSimulationPaceResponse& res);
    Result GetSimulationSettings(const sseProto::GetSimulationSettingsRequest& req, sseProto::GetSimulationSettingsResponse& res) const;

    // Simulation management
    Result StartSimulation(const sseProto::StartSimulationRequest& req, sseProto::StartSimulationResponse& res);
    Result RestartSimulation(const sseProto::RestartSimulationRequest& req, sseProto::RestartSimulationResponse& res);
    Result StopSimulation(const sseProto::StopSimulationRequest& req, sseProto::StopSimulationResponse& res);
    Result StopSimulationRequested(const sseProto::StopSimulationRequest& req, sseProto::StopSimulationResponse& res);
    Result StepSimulation(const sseProto::StepSimulationRequest& req, sseProto::StepSimulationResponse&);
    Result PauseOrResumeSimulation(const sseProto::ToggleSimulationPausedRequest& req, sseProto::ToggleSimulationPausedResponse& res);

    // Services for access simulation status and simulation time
    Result GetSimulationStatus(const sseProto::GetSimulationStatusRequest& req, sseProto::GetSimulationStatusResponse& res) const;
    Result GetSimulationTime(const sseProto::GetSimulationTimeRequest& req, sseProto::GetSimulationTimeResponse& res) const;

    // Actor management
    Result AddRuntimeActor(const sseProto::AddActorRequest& req, sseProto::AddActorResponse& res);
    Result SetRuntimeActors(const sseProto::SetRuntimeActorsRequest& req, sseProto::SetRuntimeActorsResponse& res);
    Result SetActorPoses(const sseProto::SetActorPosesRequest& req, sseProto::SetActorPosesResponse& res);
    Result SetVehiclePoses(const sseProto::SetVehiclePosesRequest& req, sseProto::SetVehiclePosesResponse& res);
    Result GetRuntimeActors(const sseProto::GetRuntimeActorsRequest& req, sseProto::GetRuntimeActorsResponse& res);
    Result GetActors(const sseProto::GetActorsRequest& req, sseProto::GetActorsResponse& res);
    Result GetActorPoses(const sseProto::GetActorPosesRequest& req, sseProto::GetActorPosesResponse& res);
    Result GetGeodeticCoordinate(const sseProto::GetGeodeticCoordinateRequest& req, sseProto::GetGeodeticCoordinateResponse& res);
    Result DoAction(const sseProto::DoActionRequest& req, sseProto::DoActionResponse& res);

    // Action management
    Result NotifyActionComplete(const sseProto::NotifyActionCompleteRequest& req, sseProto::NotifyActionCompleteResponse& res);

    // Custom event management
    Result SendEvents(const sseProto::SendEventsRequest& req, sseProto::SendEventsResponse& res);
    Result ReceiveEvents(const sseProto::ReceiveEventsRequest& req, sseProto::ReceiveEventsResponse& res);

    // Runtime phases' attributes
    Result GetPhaseStatus(const sseProto::GetPhaseStatusRequest& req, sseProto::GetPhaseStatusResponse& res);

  public:
    // public methods for other objects of scenario engine, such as engine, the
    // scenario logic executor etc.

    // Set a reference to the object that instantiates this SSE
    void SetInstantiator(SSEServerInstantiator* instantiator) {
        mInstantiator = instantiator;
    }

    // Get the reference to the object that instantiates this SSE
    SSEServerInstantiator* GetInstantiator() const {
        return mInstantiator;
    }

    // Internal utility for initializing map, scenario, and simulation settings
    Result InitializeSimulation(const sseProto::SimulationSpec& simSpec);

    // Internal utility for start the simulation
    Result StartInternal();

    // Access the map service
    // MapService& GetMapService() const { return *(mMapServicePtr.get()); }

    // Return whether the simulation is currently in-process
    bool IsSimulationRunning() const {
        std::shared_lock guard(mSimulatorMutex);
        return isRunning(mSimulator);
    }

    // Return current simulation time
    double GetTimeNow() const {
        return mEventCalendar.GetTimeNow();
    }

    // Publish scenario event
    void PublishEvent(const rrSP<sseProto::Event>& evt) const;

    // Create and initialize actors defined in a phase
    void                  InitializeActor(const Phase& phase);
    rrSP<sseProto::Actor> CreateActor(const std::string& actorId);

    // Get all actors in the simulation
    std::vector<rrSP<sseProto::Actor>> GetActorsInternal() const;
    // Find an actor with a specified ID
    rrSP<sseProto::Actor> FindActor(const std::string& actorId, bool expectExists = true) const;
    // Fina a behavior with a specified ID
    const sseProto::Behavior* FindBehavior(const std::string& actorId, bool expectExists = true) const;
    // Provided with two actors, return whether if the first actor is the parent or ancestor of the second actor
    bool IsParentOrAncestor(const sseProto::ActorRuntime& ancestor, const sseProto::ActorRuntime& child) const;
    void SetActorRuntimeLogger(ActorRunTimeLogger* pActorRtLogger);
    void SetDiagnosticMessageLogger(DiagnosticMessageLogger* pDiagnosticMsgLogger);

    DiagnosticMessageLogger* GetDiagnosticMessageLogger() {
        return mDiagnosticMsgLogger;
    }

    // Log a diagnostic message to the diagnostics logging
    void LogDiagnostic(const std::string& message, sseProto::DiagnosticType diagType, double simTime = 0.0);

  private:
    // Internal utility for updating pacer on/off status and simulation pace
    Result SetSimulationPaceInternal(bool isPacerOn, double pace);

    // Get the simulation pace
    double GetSimulationPace() const {
        return mSimPacer.GetPace();
    }

    // Get the step size
    double GetStepSize() const {
        return mStepSize;
    }

    // Get the stop time
    double GetStopTime() const {
        return mStopTime;
    }

    // Initialize runtime data of the simulation, called upon simulation start
    void Setup(const sseProto::StartSimulationRequest& req);
    // Initialize world actor, called upon simulation start
    void InitializeWorldActor();
    // Initialize misc. actors, called upon simulation start
    void InitializeMiscellaneousActors();
    // Clean up runtime data of the simulation, called upon simulation stop
    void Cleanup();
    // Clean up post simulation stop, called immediately after simulation stop
    void PostStopCleanup();

    // Process one event, return true if simulation shall resume
    bool ProcessEvent();
    // Process all events at current time, return true if simulation shall resume
    bool ProcessPresentEvents();
    // Notify all clients to stop simulation
    void PublishStopEvent(const sseProto::SimulationStopCause& cause);
    // Publish an event to notify clients that the simulation status has changed
    void NotifySimStatusChanged(sseProto::SimulationStatus status) const;

    void AddDescendants(rrSP<sseProto::Actor> parent, std::vector<rrSP<sseProto::Actor>>& desc) const;
    void AddRuntimeActors(rrSP<sseProto::Actor> actor, std::vector<rrSP<sseProto::Actor>>& descendants);

    void CommitActorUpdates();

    std::pair<Result, std::unordered_map<std::string, rrSP<sseProto::ActorRuntime>>::iterator>
         UpdateActorPose(const sseProto::ActorPose& actorPose);
    void UpdateActors(rrSP<sseProto::Actor> actor, rrSP<sseProto::Actor> parent, bool parentChanged);
    void PerformActorTypeSpecificUpdate();
    void ClearCharacterBoneTransforms();
    // bool GetIsReplayMode() { return (mReplaySimBehavior != nullptr); }
    void ProcessEventForReplay(const rrSP<sseProto::Event>& evt);
    // APIs for server-side logging
    void LogActorRuntimeState(std::unordered_map<std::string, rrSP<sseProto::ActorRuntime>>& actorRtState);
    // Log simulation event to the runtime log
    void LogSimulationEvent(const std::set<rrSP<sseProto::Event>>& userDefinedEvents);
    // Log simulation event to diagnostics
    void LogEventToDiagnostics(rrSP<sseProto::Event> event);

    ////////////////////////////////////////////////////////////////////////////
    // Class to manage custom events from actors and the supervisor (i.e., the
    // scenario logic executor).
    //
    class CustomEventQueue {
      public:
        enum class SenderType {
            eSupervisor = 0,
            eActors
        };

        std::vector<rrSP<sseProto::Event>> mEventsFromSupervisor;
        std::vector<rrSP<sseProto::Event>> mEventsFromActors;
    };

    ////////////////////////////////////////////////////////////////////////////

    void CommitEventUpdates(CustomEventQueue::SenderType senderType);

  public:
    static const std::string cWorldActorID;

  private:
    ////////////////////////////////////////////////////////////////////////////
    // Class to manage the simulation pace information and pacing
    // Users of this class have to first call RecordStartTime(), which records the
    // time point. Subsequently users have to call ApplyPacing() by passing in
    // the time. This methods returns after the delay has elapsed.
    // - not thread safe
    class SimulationPacer {
      public:
        // Set the simulation pacing on/off and pacing ratio
        // Simulation pace: SimulationPace = (elapsed Simulation time)/(elapsed wall clock time)
        Result SetSimulationPace(bool isOn, double pace);

        // Get the simulation pace
        double GetPace() const {
            return SimulationPace;
        }

        void RecordStartTime();

        void ApplyPacing(double elapsedSimTime);

        void BreakFromPacing();

      private:
        // Pacer on/off state
        std::atomic<bool> IsPacerOn = false;
        // Simulation pace: SimulationPace = (elapsed Simulation time)/(elapsed wall clock time)
        std::atomic<double> SimulationPace = 0.0;

        std::chrono::time_point<std::chrono::system_clock> StartTime;

        // CV, mutex and predicate to break the sleep cycle during pacing
        std::condition_variable AbortPacingCV;
        std::mutex              PacingLock;
        bool                    ContinueSleeping = false;
        const double            cLowerBound      = 0.05;
        const double            cUpperBound      = 20.0;
    };

    ////////////////////////////////////////////////////////////////////////////

    // A reference to scenario engine
    SimulationEventPublisher* const mPublisher;

    // A helper object to start and manage actor simulators
    // BootstrapManager* mBootstrapMgr = nullptr;

    // A reference to the object that instantiates this SSE
    SSEServerInstantiator* mInstantiator = nullptr;

    // Concurrent control for the scenario model including
    // - The world actor (that contains the logic model)
    // - The transfer map and associated map service instance
    mutable std::shared_mutex mScenarioMutex;
    // Transfer map
    sseProto::MapAndHeader mTransferMap;
    // Map service provides various map-related services during a simulation
    // rrUP<MapService> mMapServicePtr;
    // The world actor (that contains the logic model)
    rrSP<sseProto::Actor> mWorld;

    // Manage concurrent access of simulation settings
    mutable std::shared_mutex mSimulationSettingsMutex;
    // Simulation settings
    sseProto::SimulationSettings mSimulationSettings;

    // Manage concurrent access of the runtime simulation model
    mutable std::shared_mutex mSimulationModelMutex;
    // Map for fast look-up from an actor id to the prototype actor
    std::unordered_map<std::string, const sseProto::Actor*> mActorMap;
    // Map for fast look-up from a behavior id to the behavior
    std::unordered_map<std::string, const sseProto::Behavior*> mBehaviorMap;
    // Map for storing character assets that have been loaded for this simulation
    // - NOTE: character assets can be re-used between multiple instances of a character actor,
    //         and the key here is the asset_reference(), not the actor ID.
    // std::unordered_map<std::string, rrSP<const roadrunner::animation::CharacterAsset>> mCharacterAssetMap;
    // The run-time scenario executor
    rrUP<ScenarioExecutor> mScenario;
    // Event calendar
    EventCalendar mEventCalendar;
    // Simulation pacer for pacing simulation
    SimulationPacer mSimPacer;

    // Run-time engine
    mutable std::shared_mutex mSimulatorMutex;
    std::future<void>         mSimulator;

    // Future to support simulation restart
    mutable std::shared_mutex mRestarterMutex;
    std::future<void>         mRestarter;

    // Flag to notify simulation to stop
    mutable std::mutex            mStopCauseMutex;
    sseProto::SimulationStopCause mStopCause;
    std::atomic<bool>             mRequestToStop = false;

    // Zero value of this counter indicates that simulation is not in
    // single-stepping mode. Non-zero value of this counter indicates that
    // simulation is in single-stepping mode, and the value records how many
    // stepping requests are to be processed
    std::atomic<size_t> mNumStepRequests = 0;

    // Flag to notify simulation to pause
    mutable std::mutex         mSimStatusMutex;
    sseProto::SimulationStatus mSimulationStatus = sseProto::SimulationStatus::SIMULATION_STATUS_STOPPED;
    bool                       mIsPaused         = false;
    bool                       mWasPaused        = false;
    std::condition_variable    mIsPausedCV;

    // Manage concurrent access via a shared multi-read, single-write lock
    mutable std::shared_mutex ActorMutex;
    // Run-time actors
    std::unordered_map<std::string, rrSP<sseProto::Actor>> RtActors;
    rrSP<sseProto::Actor>                                  mRtWorldActor;
    // Uncommitted actor changes
    std::unordered_map<std::string, rrSP<sseProto::ActorRuntime>> UnCommittedActors;
    // A set of actor poses that need to convert to world coordinates
    std::unordered_set<std::string> mWorldCoordSet;
    // Map for storing character runtime state
    // std::unordered_map<std::string, rrSP<roadrunner::animation::CharacterRuntime>> mCharacterRuntimeMap;

    // Manage concurrent access via a shared multi-read, single-write lock
    mutable std::shared_mutex mCustomEventMutex;
    // Map from custom event name to its parameter definition
    // - All run-time custom events are validated against these definitions
    std::unordered_map<std::string, roadrunner::proto::CustomEventDefinition> mCustomEventDefinitionMap;
    // Run-time custom events
    std::unordered_map<std::string, CustomEventQueue> mCustomEventMap;

    // Uncommitted custom events
    struct EventPtrComp {
        bool operator()(const rrSP<sseProto::Event>& a, const rrSP<sseProto::Event>& b) const {
            if (a->sender_id().empty()) {
                return true;
            }
            if (b->sender_id().empty()) {
                return false;
            }
            return (boost::lexical_cast<uint64_t>(a->sender_id()) <= boost::lexical_cast<uint64_t>(b->sender_id()));
        }
    };

    // Events are sorted by sender id, i.e. sender id: empty ---> 0 ---> 1 ---> 2.
    typedef std::set<rrSP<sseProto::Event>, EventPtrComp> SortedEventList;
    std::unordered_map<std::string, SortedEventList>      mUnCommittedCustomEvents;
    // Most recent custom event source (i.e., either from actors or from the supervisor)
    CustomEventQueue::SenderType mLastSender = CustomEventQueue::SenderType::eSupervisor;

    // Number of steps taken since start of simulation
    unsigned int mNumSteps = 0;
    // Step size (in seconds)
    double mStepSize = 0.02;
    // Stop time (in seconds)
    double mStopTime = 10.0;

    // A pointer to an actor runtime data logger
    ActorRunTimeLogger* mActorRtLogger = nullptr;

    // Declare friend class for testing
    friend class SimulationTestFixture;
    friend class ScenarioLogicTestFixture;

    // Replay simulation behavior
    // rrUP<ReplayBehavior> mReplaySimBehavior;

    // A pointer to an diagnostic logger
    DiagnosticMessageLogger* mDiagnosticMsgLogger = nullptr;
};
} // namespace sse
