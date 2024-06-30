#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"

#include "Simulation.h"

#include "EventPublisher.h"
// #include "Map.h"
#include "MessageLogger.h"
// #include "ReplayBehavior.h"
#include "Phase.h"
#include "PhaseDiagramExecutor.h"
#include "RoadRunnerConversions.h"
#include "ScenarioConfiguration.h"
#include "SimulationServer.h"
#include "Utility.h"
#include "ScenarioUtils.h"
#include "Util.h"

using namespace sseProto;
using namespace rrProtoGrpc;

namespace sse {

const std::string Simulation::cWorldActorID = "0";

////////////////////////////////////////////////////////////////////////////////

Simulation::Simulation(SimulationEventPublisher* publisher)
    : mPublisher(publisher)
    , mSimulationSettings(sse::ScenarioConfiguration::GetDefaultSimulationSettings()) {
}

////////////////////////////////////////////////////////////////////////////////

Simulation::~Simulation() {
    {
        std::shared_lock guard(mRestarterMutex);
        if (isRunning(mRestarter)) {
            {
                std::lock_guard lock(mStopCauseMutex);
                mStopCause.mutable_simulation_complete()->mutable_stop_requested();
            }
            mRequestToStop = true;
            mRestarter.wait();
        }
    }

    {
        std::shared_lock guard(mSimulatorMutex);
        if (isRunning(mSimulator)) {
            StopSimulationRequest  stopReq;
            StopSimulationResponse stopRes;
            StopSimulation(stopReq, stopRes);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::InitializeSimulation(const SimulationSpec& simSpec) {
    Result result;
    {
        // Initialize map
        UploadMapRequest  request;
        UploadMapResponse response;
        request.mutable_map_and_header()->CopyFrom(simSpec.map_and_header());
        Result r = UploadMap(request, response);
        if (r.Type == Result::EnumType::eError) {
            result.AddError(r.Message);
        }
    }
    {
        // Initialize scenario
        UploadScenarioRequest  request;
        UploadScenarioResponse response;
        request.mutable_world_actor()->CopyFrom(simSpec.world_actor());
        Result r = UploadScenario(request, response);
        if (r.Type == Result::EnumType::eError) {
            result.AddError(r.Message);
        }
    }
    {
        // Initialize simulation settings
        SetSimulationSettingsRequest  request;
        SetSimulationSettingsResponse response;
        request.mutable_simulation_settings()->CopyFrom(simSpec.simulation_settings());
        Result r = SetSimulationSettings(request, response);
        if (r.Type == Result::EnumType::eError) {
            result.AddError(r.Message);
        }
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::StartInternal() {
    // Start simulation
    StartSimulationRequest  request;
    StartSimulationResponse response;
    request.set_single_stepping(false);
    return StartSimulation(request, response);
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::UploadMap(const UploadMapRequest& request, UploadMapResponse& /*res*/) {
    // Validate that a map is contained in the upload request
    if (!request.map_and_header().has_map() || !request.map_and_header().has_header()) {
        return Result::Error("Request must contain a map and a header field");
    }

    {
        std::unique_lock lock(mScenarioMutex);

        // Map cannot be changed if any simulation is still running
        if (IsSimulationRunning()) {
            return Result::Error("Cannot upload map while a simulation is running");
        }

        // Copy over map and additional upload fields (header)
        mTransferMap = request.map_and_header();

        /*
        try {
            // Decode map into a transfer map object
            auto decodeHeader = coTransferMapSerialize::DecodeHeader(mTransferMap.header());
            coProtoContext context;
            context.Bounds = decodeHeader->Bounds;
            auto transferModel = coTransferMapSerialize::DecodeTransferMap(mTransferMap.map(), context);

            // Create the map service provider
            mMapServicePtr = std::make_unique<MapService>(std::move(transferModel));
        }
        catch (const rrException& e)
        {
            rrError("Failed to upload map - " + e.ToString());
            return Result::Error(e.what());
        }
        */
    }

    // Notify clients that scene has changed
    auto evt = EventCalendar::CreateSceneChangedEvent();
    PublishEvent(evt);

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::DownloadMap(const DownloadMapRequest& /*request*/, DownloadMapResponse& res) const {
    std::shared_lock lock(mScenarioMutex);

    res.mutable_map_and_header()->mutable_map()->CopyFrom(mTransferMap.map());
    res.mutable_map_and_header()->mutable_header()->CopyFrom(mTransferMap.header());

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::UploadScenario(const UploadScenarioRequest& req, UploadScenarioResponse&) {
    // Validate that a world actor is contained in the upload request
    const Actor& actor = req.world_actor();
    if (!actor.actor_spec().has_world_spec()) {
        return Result::Error("Request must contain a world actor");
    }

    // Validate the world actor
    const auto& worldSpec = actor.actor_spec().world_spec();
    /*if (worldSpec.coordinate_system().has_geographic_coordinate_system()) {
            return Result::Error("Geographic coordinate system is not supported for a world actor");
    }*/

    {
        std::unique_lock guard(mScenarioMutex);

        if (IsSimulationRunning()) {
            return Result::Error("Cannot upload scenario while a simulation is running");
        }

        mWorld = std::make_shared<Actor>(actor);
    }

    // Notify clients that scenario has changed
    auto evt = EventCalendar::CreateScenarioChangedEvent();
    PublishEvent(evt);

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::DownloadScenario(const DownloadScenarioRequest&, DownloadScenarioResponse& res) const {
    std::shared_lock guard(mScenarioMutex);

    if (mWorld && mWorld->actor_spec().has_world_spec()) {
        res.mutable_world_actor()->CopyFrom(*mWorld);
    } else {
        return Result::Error("Scenario does not exist");
    }
    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::SetSimulationSettings(const SetSimulationSettingsRequest& req,
                                         SetSimulationSettingsResponse&) {
    // Validate the proposed simulation settings
    const SimulationSettings& settings = req.simulation_settings();
    try {
        // Check step size
        ::Util::CheckSimulationStepSize(settings.step_size());

        // Check max simulation time
        ::Util::CheckMaxSimulationTime(settings.max_simulation_time());

        // If pacer is on, check simulation pace
        if (req.simulation_settings().is_pacer_on()) {
            ::Util::CheckSimulationPace(settings.simulation_pace());
        }
    } catch (const rrException& ex) {
        return Result::Error(ex.what());
    }

    // Disallow change of step size and maximal simulation time when a simulation is running
    Result result;
    bool   setTimeVars = true;
    if (IsSimulationRunning()) {
        setTimeVars = false;
        std::shared_lock guard(mSimulationSettingsMutex);
        if (settings.step_size() != mSimulationSettings.step_size()) {
            result = Result::Error("Cannot change simulation step size while simulation is running");
        } else if (settings.max_simulation_time() != mSimulationSettings.max_simulation_time()) {
            result = Result::Error("Cannot change simulation stop time while simulation is running");
        }
    }

    // Update step size and maximal simulation time
    if (setTimeVars) {
        std::unique_lock guard(mSimulationSettingsMutex);
        mSimulationSettings.set_step_size(settings.step_size());
        mSimulationSettings.set_max_simulation_time(settings.max_simulation_time());
    }

    // Update simulation pace
    SetSimulationPaceInternal(settings.is_pacer_on(), settings.simulation_pace());

    // Notify clients that simulation settings have changed
    auto evt = EventCalendar::CreateSimulationSettingsChangedEvent(settings);
    PublishEvent(evt);

    return result;
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::SetSimulationPace(const SetSimulationPaceRequest& req,
                                     SetSimulationPaceResponse&) {
    Result res = SetSimulationPaceInternal(req.is_pacer_on(), req.simulation_pace());
    if (res.Type != Result::EnumType::eSuccess) {
        return res;
    }

    // Notify clients that simulation settings have changed
    rrSP<Event> evt;
    {
        std::shared_lock guard(mSimulationSettingsMutex);
        evt = EventCalendar::CreateSimulationSettingsChangedEvent(mSimulationSettings);
    }
    PublishEvent(evt);

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::SetSimulationPaceInternal(bool isPacerOn, double pace) {
    // Update the ground-truth simulation settings
    {
        std::unique_lock guard(mSimulationSettingsMutex);
        mSimulationSettings.set_is_pacer_on(isPacerOn);
        mSimulationSettings.set_simulation_pace(pace);
    }

    mSimPacer.BreakFromPacing();

    // Set the new simulation pace
    return mSimPacer.SetSimulationPace(isPacerOn, pace);
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::GetSimulationSettings(const GetSimulationSettingsRequest&,
                                         GetSimulationSettingsResponse& res) const {
    std::shared_lock guard(mSimulationSettingsMutex);
    res.mutable_simulation_settings()->CopyFrom(mSimulationSettings);

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::Setup(const StartSimulationRequest& req) {
    google::protobuf::RepeatedPtrField<std::string> excludedIds;
    // If replay mode, instantiate replay mode behavior
    if (req.has_replay_mode_simulation()) {
        excludedIds = req.replay_mode_simulation().excluded_ids();
        // mReplaySimBehavior = std::make_unique<ReplayBehavior>(req, mSimulationSettings, this);
    }

    mPublisher->SetReplayMode(req.has_replay_mode_simulation(), excludedIds);

    // Initialize the bootstrap manager
    // Pass World info to Bootstrap manager
    // mBootstrapMgr->LoadRuntimeWorld(*mWorld);

    //// Gather platform requirements from Scenario/Actor info
    // mBootstrapMgr->CompilePlatformRequirement();

    // Prevent another thread to read or write the simulation model while it is initializing
    std::unique_lock simModelLock(mSimulationModelMutex);

    // Populate the prototype actor map and behavior map
    const WorldSpec& worldSpec = mWorld->actor_spec().world_spec();

    mActorMap.clear();
    // Register world actor
    mActorMap.insert({mWorld->actor_spec().id(), mWorld.get()});

    // Register other actors
    int num = worldSpec.actors_size();
    // mCharacterAssetMap.clear();
    for (int i = 0; i < num; ++i) {
        const Actor* pa = &worldSpec.actors(i);
        /*if (pa->actor_spec().has_character_spec())
        {
                // load character assets?
                auto relativePath = pa->actor_spec().asset_reference();

                if (!relativePath.empty() && !stdx::ContainsKey(mCharacterAssetMap, relativePath))
                {
                        auto assetPath = rrAssetPath(rrToStr(relativePath));
                        auto assetRef = assetPath.FetchAssetRef(false);
                        auto characterAsset = roadrunner::animation::CharacterAssetProvider::Load(assetRef);
                        mCharacterAssetMap.insert({ relativePath, characterAsset });
                }
        }*/
        mActorMap.insert({pa->actor_spec().id(), pa});
    }
    num = worldSpec.behaviors_size();
    mBehaviorMap.clear();
    for (int i = 0; i < num; ++i) {
        const Behavior* b = &worldSpec.behaviors(i);
        mBehaviorMap.insert({b->id(), b});
    }

    // Populate the user-defined events map
    {
        std::unique_lock customCmdLock(mCustomEventMutex);

        num = worldSpec.scenario().custom_events_size();
        mCustomEventMap.clear();
        mCustomEventDefinitionMap.clear();
        for (int i = 0; i < num; ++i) {
            const auto& customCommand = worldSpec.scenario().custom_events(i);
            const auto& eventName     = customCommand.name();
            rrThrowVerify(!eventName.empty(),
                          "Invalid name for custom event. "
                          "A custom event must have a name.");

            roadrunner::proto::CustomEventDefinition eventDefinition{customCommand};
            auto                                     p1 = mCustomEventDefinitionMap.insert({eventName, eventDefinition});
            auto                                     p2 = mCustomEventMap.insert({eventName, {}});
            rrThrowVerify(p1.second && p2.second, "Invalid name for custom event " + eventName +
                                                      ". Another custom "
                                                      "event with the same name already exists.");
        }
    }

    // Create the run-time scenario executor
    mScenario = ScenarioExecutor::CreateScenarioExecutor(*mWorld, this);

    {
        std::shared_lock simSettingLock(mSimulationSettingsMutex);

        // Set step size
        mStepSize = mSimulationSettings.step_size();
        // Stop time
        mStopTime = mSimulationSettings.max_simulation_time();
        // Set pacer on/off state and simulation pace
        mSimPacer.SetSimulationPace(mSimulationSettings.is_pacer_on(), mSimulationSettings.simulation_pace());
    }

    // Initialize the event calendar with a set of events

    // Start event
    mEventCalendar.ScheduleEvent(0, EventCalendar::CreateSimulationStartEvent(req.has_replay_mode_simulation()));

    // The 1st step event
    mEventCalendar.ScheduleEvent(0, EventCalendar::CreateSimulationStepEvent(0, 0));

    // The 1st scenario update event
    mEventCalendar.ScheduleEvent(0, EventCalendar::CreateScenarioUpdateEvent());

    // The 1st post-step event
    mEventCalendar.ScheduleEvent(0, EventCalendar::CreateSimulationPostStepEvent());

    // Stop event
    SimulationStopCause cause;
    cause.mutable_simulation_complete()->mutable_stop_time_reached();
    cause.set_summary("Simulation ended: Maximum simulation time of " + std::to_string(mStopTime) + " seconds reached.");
    mEventCalendar.ScheduleEvent(mStopTime, EventCalendar::CreateSimulationStopEvent(mStopTime, 0, cause));
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::Cleanup() {
    {
        std::unique_lock simModelLock(mSimulationModelMutex);

        // Reset number of step taken
        mNumSteps = 0;

        // Cleanup event calendar
        mEventCalendar.Cleanup();

        // Cleanup scenario executor
        mScenario.reset();
    }

    // Clean up runtime actors
    {
        std::unique_lock actorLock(ActorMutex);
        RtActors.clear();
        mRtWorldActor.reset();
        UnCommittedActors.clear();
        mWorldCoordSet.clear();
        // mCharacterRuntimeMap.clear();
    }

    // Clean up custom events
    {
        std::unique_lock customCmdLock(mCustomEventMutex);
        mCustomEventMap.clear();
        mCustomEventDefinitionMap.clear();
        mUnCommittedCustomEvents.clear();
        mLastSender = CustomEventQueue::SenderType::eSupervisor;
    }

    PostStopCleanup();

    // Reset the replay mode
    mPublisher->SetReplayMode(false, {});
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::PostStopCleanup() {
    // Reset replay simulation behavior
    /*if(mReplaySimBehavior)
            mReplaySimBehavior.reset(nullptr);*/
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::InitializeMiscellaneousActors() {
    /*for (const auto& actor : mWorld->actor_spec().world_spec().actors())
    {
            if (!actor.actor_spec().has_miscellaneous_spec())
                    continue;

            // Create the actor by copying the actor specification
            rrSP<Actor> rtActor = std::make_shared<Actor>(actor);

            std::vector<rrSP<Actor>> descendents;
            sseProto::Phase initPhase;

            // Publish a create actor event to notify actor creation
            rrSP<Event> evt = EventCalendar::CreateCreateActorEvent(*rtActor, descendents, initPhase);
            PublishEvent(evt);

            AddRuntimeActors(rtActor, descendents);
    }*/
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::InitializeWorldActor() {
    if (!mRtWorldActor) {
        mRtWorldActor = std::make_shared<Actor>(*mActorMap[cWorldActorID]);
    }
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::StartSimulation(const StartSimulationRequest& req, StartSimulationResponse&) {
    // Early return if simulation has already started
    if (IsSimulationRunning()) {
        return Result::Error("Simulation has already started");
    }

    std::shared_lock guard(mScenarioMutex);

    // Early return if a valid map has not yet been uploaded
    /*if (!mMapServicePtr) {
            return Result::Error("No valid map to simulate");
    }*/

    // Early return if a valid scenario has not yet been uploaded
    if (!(mWorld && mWorld->actor_spec().has_world_spec())) {
        return Result::Error("No valid scenario to simulate");
    }

    auto simLoop = [&]() {
        // Update simulation status to running
        {
            std::lock_guard lock(mSimStatusMutex);
            mSimulationStatus = SimulationStatus::SIMULATION_STATUS_RUNNING;
            NotifySimStatusChanged(mSimulationStatus);
            // NotifySimulationStartedInternal(); // Emits a Qt signal
        }

        // Check if we need bootstrapping of MATLAB / CARLA. If so, block until
        // the required client is subscribed and resume the simulation;
        // otherwise, do nothing
        // mBootstrapMgr->DoBootstrapIfApplicable();

        InitializeWorldActor();
        InitializeMiscellaneousActors();

        bool resumeSim = true;
        bool startDone = false;
        while (true) {
            {
                try {
                    if (startDone) {
                        // The start event has already been processed
                        resumeSim = ProcessPresentEvents();
                        if (!resumeSim) {
                            break;
                        }
                    } else {
                        // Process the start event
                        ProcessEvent();
                        startDone = true;
                    }
                } catch (const ScenarioSuccessException& e) {
                    SimulationStopCause cause;
                    cause.set_summary(e.mSummary);
                    cause.mutable_simulation_complete()->mutable_success_status()->CopyFrom(e.mSuccessStatus);
                    PublishStopEvent(cause);
                    break;
                } catch (const ScenarioFailureException& e) {
                    SimulationStopCause cause;
                    cause.set_summary(e.mSummary);
                    cause.mutable_simulation_failed()->mutable_failure_status()->CopyFrom(e.mFailureStatus);
                    PublishStopEvent(cause);
                    break;
                } catch (const rrException& e) {
                    SimulationStopCause cause;
                    cause.set_summary(e.HasCustomMessage() ? e.ToString() : e.what());
                    cause.mutable_simulation_failed()->mutable_engine_error();
                    PublishStopEvent(cause);
                    break;
                } catch (const std::exception& e) {
                    SimulationStopCause cause;
                    cause.set_summary("Scenario Engine error. " + std::string(typeid(e).name()));
                    cause.mutable_simulation_failed()->mutable_engine_error();
                    PublishStopEvent(cause);
                    break;
                }
            }

            // Process changes in simulation status
            {
                if (mRequestToStop) {
                    mRequestToStop = false;
                    std::lock_guard guard(mStopCauseMutex);
                    PublishStopEvent(mStopCause);
                    break;
                }
                std::unique_lock guard(mSimStatusMutex);
                if (mNumStepRequests > 0) {
                    if (--mNumStepRequests == 0) {
                        mIsPaused = true;
                    }
                }
                if (mIsPaused) {
                    mSimulationStatus = SimulationStatus::SIMULATION_STATUS_PAUSED;
                    NotifySimStatusChanged(mSimulationStatus);

                    mIsPausedCV.wait(guard, [&] { return !mIsPaused; });

                    if (mRequestToStop) {
                        mRequestToStop = false;
                        std::lock_guard guard(mStopCauseMutex);
                        PublishStopEvent(mStopCause);
                        break;
                    }

                    mSimulationStatus = SimulationStatus::SIMULATION_STATUS_RUNNING;
                    NotifySimStatusChanged(mSimulationStatus);
                }
            }
        }

        Cleanup();

        // Update simulation status to stopped
        //? TODO - This should be outside the loop using a QFutureWatcher to
        // watch the QFuture for the finished signal, and a connection to trigger
        // this call but that requires this to be run on a thread with a
        // QEventLoop. There's a race condition between any code responding to
        // this and the future actually showing !Engine.isRunning(), which is
        // used most everywhere.
        {
            std::lock_guard lock(mSimStatusMutex);
            mSimulationStatus = SimulationStatus::SIMULATION_STATUS_STOPPED;
            NotifySimStatusChanged(mSimulationStatus);
        }
    };

    // Start simulation
    try {
        Setup(req);

        mRequestToStop   = false;
        mNumStepRequests = req.start_and_pause() ? 1 : (req.single_stepping() ? 2 : 0);
        {
            std::lock_guard  simStatusLock(mSimStatusMutex);
            std::shared_lock restarterLock(mRestarterMutex);
            mIsPaused = isRunning(mRestarter) ? mWasPaused : false;
        }

        std::unique_lock executorLock(mSimulatorMutex);
        mSimulator = std::async(std::launch::async, simLoop);
    } catch (const rrException& e) {
        rrError(e.what());
        return Result::Error(e.what());
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::RestartSimulation(const RestartSimulationRequest&, RestartSimulationResponse&) {
    // Early return if another restart is on-going
    {
        std::shared_lock restarterLock(mRestarterMutex);
        if (isRunning(mRestarter)) {
            return Result::Error("Restart is already on-going.");
        }
    }

    {
        std::lock_guard simStatusLock(mSimStatusMutex);
        mWasPaused = mIsPaused;
    }

    // Request simulation to stop
    StopSimulationRequest  stopReq;
    StopSimulationResponse stopRes;
    Result                 result = StopSimulation(stopReq, stopRes);
    if (result.Type == Result::EnumType::eError) {
        return result;
    }

    // Run a future to restart simulation
    auto restartFcn = [&]() {
        {
            std::shared_lock simulatorLock(mSimulatorMutex);
            mSimulator.wait();
        }
        if (!mRequestToStop) {
            StartSimulationRequest  startReq;
            StartSimulationResponse startRes;
            StartSimulation(startReq, startRes);
        }
    };
    {
        std::unique_lock restarterLock(mRestarterMutex);
        mRestarter = std::async(std::launch::async, restartFcn);
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::StopSimulation(const StopSimulationRequest& req, StopSimulationResponse& res) {
    auto result = StopSimulationRequested(req, res);

    // Wait until the engine loop is finished
    {
        std::shared_lock guard(mSimulatorMutex);
        if (isRunning(mSimulator)) {
            mSimulator.wait();
        }
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::StopSimulationRequested(const StopSimulationRequest& req, StopSimulationResponse&) {
    std::shared_lock guard(mSimulatorMutex);

    // Early return if simulation is not running
    if (!isRunning(mSimulator)) {
        return Result::Error("Simulation has not started");
    }

    // Record stop cause
    {
        std::lock_guard lock(mStopCauseMutex);
        mStopCause = req.cause();
    }

    // In case the engine is sleeping to account for pacing, break out from pacing
    {
        // Signal simulation thread to stop
        mRequestToStop = true;
        mSimPacer.BreakFromPacing();
    }

    // Unblock the simulation thread if it is in pause state
    {
        std::lock_guard lock(mSimStatusMutex);
        if (mIsPaused) {
            mIsPaused = false;
            mIsPausedCV.notify_one();
        }
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::StepSimulation(const StepSimulationRequest&, StepSimulationResponse&) {
    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    // To enter single stepping mode, simulation must be in paused state
    std::lock_guard lock(mSimStatusMutex);
    if (mNumStepRequests == 0 && !mIsPaused) {
        return Result::Error("Simulation must be paused before single stepping");
    }

    // Notify simulation to continue and take one time-step
    ++mNumStepRequests;
    mIsPaused = false;
    mIsPausedCV.notify_one();

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::PauseOrResumeSimulation(const ToggleSimulationPausedRequest& req, ToggleSimulationPausedResponse&) {
    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    std::lock_guard guard(mSimStatusMutex);

    if (!req.pause_simulation()) {
        // Request to resume simulation
        if (mNumStepRequests > 0) {
            // Quit single stepping mode
            mNumStepRequests = 0;
        } else if (mIsPaused) {
            // Quit pause mode
            mIsPaused = false;
            mIsPausedCV.notify_one();
        }
    } else {
        // Request to pause
        if (!mIsPaused) {
            // Pause simulation
            mIsPaused = true;
        }
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::GetSimulationStatus(const GetSimulationStatusRequest&, GetSimulationStatusResponse& res) const {
    std::lock_guard guard(mSimStatusMutex);

    res.set_simulation_status(mSimulationStatus);

    return Result::Success();
}

Result Simulation::GetSimulationTime(const GetSimulationTimeRequest&, GetSimulationTimeResponse& res) const {
    std::shared_lock guard(mSimulationModelMutex);

    res.set_simulation_time(GetTimeNow());
    res.set_steps(mNumSteps);
    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

bool Simulation::ProcessPresentEvents() {
    bool resumeSim = false;

    // Record the current time for simulation pacing
    mSimPacer.RecordStartTime();

    // Process all events at current time
    while (mEventCalendar.HasPresentEvent()) {
        resumeSim = ProcessEvent();
        if (!resumeSim) {
            break;
        }
    }

    // Promote simulation clock if simulation is not completed
    if (resumeSim) {
        double timeStep = mEventCalendar.AdvanceTime();

        std::cout << "Advanced simulation time to: " << timeStep << std::endl;

        // Apply pacing
        mSimPacer.ApplyPacing(timeStep);
    }

    return resumeSim;
}

////////////////////////////////////////////////////////////////////////////////

bool Simulation::ProcessEvent() {
    bool        resumeSim = false;
    rrSP<Event> evt       = mEventCalendar.PopNextEvent();
    if (evt.get()) {
        if (evt->has_simulation_stop_event()) {
            SimulationStopEvent* evtStop = evt->mutable_simulation_stop_event();
            evtStop->set_steps(mNumSteps);
        } else if (evt->has_simulation_step_event()) {
            // Character bone transforms are cleared every frame so that the SSE
            // computes a new pose if no client submits transforms for that character.
            ClearCharacterBoneTransforms();
            resumeSim = true;
        } else {
            // Request to resume simulation if no stop has been requested
            resumeSim = true;
        }

        // Notify clients the occurrence of this event
        PublishEvent(evt);

        // Process event for replay
        ProcessEventForReplay(evt);

        // Notify scenario executor the occurrence of this event. Runtime status
        // of scenario logic can be viewed as derived states from world states
        // thus should be updated as soon as world states are updated.
        // - Update such status after a step event will allow the step event to
        //   observe scenario logic status prior to world state changes
        // - Post-step event will observe updated scenario logic status of this
        //   step
        mScenario->ProcessEvent(evt);

        // Additional operations after an event is published
        if (evt->has_simulation_start_event()) {
            // Additional operations for start event
            CommitEventUpdates(CustomEventQueue::SenderType::eSupervisor);
        } else if (evt->has_simulation_step_event()) {
            // Update actors' map locations and commit actor state updates
            CommitActorUpdates();

            // Commit custom events sent by actors
            CommitEventUpdates(CustomEventQueue::SenderType::eActors);

            // Renew step event
            SimulationStepEvent* stepEvt = evt->mutable_simulation_step_event();
            {
                std::unique_lock guard(mSimulationModelMutex);
                mNumSteps = stepEvt->steps();
            }
            unsigned int NextNumSteps = (mNumSteps + 1);
            double       nextStepTime = mStepSize * NextNumSteps;
            stepEvt->set_elapsed_time_seconds(nextStepTime);
            stepEvt->set_steps(NextNumSteps);
            mEventCalendar.ScheduleEvent(nextStepTime, evt);
        } else if (evt->has_scenario_update_event()) {
            // Commit custom events sent by the supervisor
            CommitEventUpdates(CustomEventQueue::SenderType::eSupervisor);

            // Renew scenario-logic step event
            mEventCalendar.ScheduleEvent(mStepSize * (mNumSteps + 1), evt);
        } else if (evt->has_simulation_post_step_event()) {
            // Renew post-step event
            mEventCalendar.ScheduleEvent(mStepSize * (mNumSteps + 1), evt);
        }
    }

    return resumeSim;
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::PublishStopEvent(const SimulationStopCause& cause) {
    double      stopTime = mNumSteps * mStepSize;
    rrSP<Event> evt      = EventCalendar::CreateSimulationStopEvent(
        stopTime,
        mNumSteps,
        cause);
    mScenario->ProcessEvent(evt);
    PublishEvent(evt);
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::PublishEvent(const rrSP<sseProto::Event>& evt) const {
    if (mPublisher) {
        if (evt->need_set_ready()) {
            mPublisher->SynchronousPublishAndWait(*evt);
        } else {
            mPublisher->BroadcastEvent(*evt);
        }
    } else {
        rrError("Event publisher not set.");
    }
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::InitializeActor(const sse::Phase& phase) {
    const sseProto::Phase&            proto      = phase.GetDataModel();
    const sseProto::ActorActionPhase& actorPhase = proto.action_phase().actor_action_phase();
    const std::string&                actorId    = actorPhase.actor_id();
    sseProto::ActorCreationMode       mode       = actorPhase.creation_mode();

    // Special handle for world actor, i.e. runtime world actor only has runtime parameters
    if (actorId == cWorldActorID) {
        InitializeWorldActor();
        return;
    }

    // Early return if the actor already exists
    rrSP<Actor> actorRef   = FindActor(actorId, false);
    bool        actorExist = (actorRef.get() != nullptr);
    switch (mode) {
    case sseProto::ACTOR_CREATION_MODE_UNSPECIFIED:
    case sseProto::ACTOR_CREATION_MODE_AUTOMATIC:
        if (actorExist) {
            return;
        }
        break;
    case sseProto::ACTOR_CREATION_MODE_CREATE:
        rrThrowVerify(!actorExist,
                      "Inconsistent state encountered. " + proto.id() + " is configured to create the " + actorId +
                          ". \
However, this actor has already been created.");
        break;
    case sseProto::ACTOR_CREATION_MODE_NO_CREATE:
        rrThrowVerify(actorExist,
                      "Inconsistent state encountered. " + proto.id() +
                          " is configured to operate on an \
existing " + actorId + ". However, this actor has not yet been created.");
        return;
    default:
        rrThrow("Invalid actor creation mode in " + proto.id() + ".");
    }

    // Actor does not exist, create and initialize it
    // - Return null if actor is existing or if actor's behavior will be
    //   provided by its parent actor
    rrSP<Actor> rtActor = CreateActor(actorId);
    if (!rtActor) {
        return;
    }

    // Initialize the actor by enforcing initial modifiers
    phase.InitializeActor(*rtActor);

    // Get the list of descendant actors that shall be created in the same event
    std::vector<rrSP<Actor>> descendants;
    AddDescendants(rtActor, descendants);

    // Publish a create actor event to notify actor creation
    rrSP<Event> evt = EventCalendar::CreateCreateActorEvent(*rtActor, descendants, const_cast<sseProto::Phase*>(&proto));
    PublishEvent(evt);

    AddRuntimeActors(rtActor, descendants);

    // Commit any updates from simulation clients on the new actor
    CommitActorUpdates();
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::AddDescendants(rrSP<Actor> parent, std::vector<rrSP<Actor>>& descendants) const {
    int numChildren = parent->actor_runtime().children_size();
    for (int i = 0; i < numChildren; ++i) {
        auto childId = parent->actor_runtime().children(i);
        auto iter    = mActorMap.find(childId);
        rrThrowVerify(iter != mActorMap.end(),
                      "Fail to create actor with id: " + childId + ". Actor specification does not exist.");
        auto childActor = std::make_shared<Actor>(*(iter->second));
        descendants.push_back(childActor);
        AddDescendants(childActor, descendants);
    }
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::AddRuntimeActors(rrSP<sseProto::Actor> actor, std::vector<rrSP<Actor>>& descendants) {
    // Add the newly created actor to runtime actor repository
    auto id = actor->actor_spec().id();
    RtActors.insert({id, actor});

    /*auto addCharacter = [this](rrSP<sseProto::Actor> actor)
    {
            if (!actor->actor_spec().has_character_spec())
                    return;
            auto iter = mCharacterAssetMap.find(actor->actor_spec().asset_reference());
            rrThrowVerify(iter != mCharacterAssetMap.end(), "Failed to find asset for %1"_Q % ActorUrl(*actor));
            auto characterRuntime = roadrunner::animation::CharacterRuntimeFactory::CreateCharacterRuntime(iter->second);
            characterRuntime->Initialize(roadrunner::proto::Util::GetSpeed(*actor));
            mCharacterRuntimeMap.insert({ actor->actor_spec().id(),  characterRuntime });
    };

    addCharacter(actor);*/

    // Initialize actor map locations
    /*GetMapService().Update(actor);*/

    // Add descendant actors
    for (auto& descedant : descendants) {
        RtActors.insert({descedant->actor_spec().id(), descedant});
        /*GetMapService().Update(descedant);

        addCharacter(descedant);*/
    }
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::AddRuntimeActor(const AddActorRequest& req, AddActorResponse&) {
    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    Result result;
    auto   actorId = req.actor_id();

    // Create actor
    // - Return null if actor is existing or if actor's behavior will be
    //   provided by its parent actor
    rrSP<Actor> rtActor = CreateActor(actorId);
    if (!rtActor) {
        return result;
    }

    // Get the list of descendant actors that shall be created in the same event
    std::vector<rrSP<Actor>> descendants;
    AddDescendants(rtActor, descendants);

    // Publish a create actor event to notify actor creation
    rrSP<Event> evt = EventCalendar::CreateCreateActorEvent(*rtActor, descendants);
    PublishEvent(evt);

    {
        std::unique_lock lock(ActorMutex);
        AddRuntimeActors(rtActor, descendants);
    }

    // Commit any updates from simulation clients on the new actor
    CommitActorUpdates();

    return result;
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::DoAction(const sseProto::DoActionRequest& req, sseProto::DoActionResponse& res) {
    Result result;

    const auto& actions = req.actions();
    rrSP<Event> actionEvt = EventCalendar::CreateActionEvent(actions);
    PublishEvent(actionEvt);

    return result;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Actor> Simulation::CreateActor(const std::string& actorId) {
    // Early return if the actor already exists
    rrSP<Actor> actorRef   = FindActor(actorId, false);
    bool        actorExist = (actorRef.get() != nullptr);
    if (actorExist) {
        return nullptr;
    }

    // Actor does not exist, create and initialize it
    auto iter = mActorMap.find(actorId);
    rrThrowVerify(iter != mActorMap.end(),
                  "Fail to create " + actorId + ". Actor specification does not exist.");

    // Do nothing if the actor's behavior is provided by its parent actor
    const Actor& actor = *(iter->second);
    if (actor.actor_runtime().parent() != mWorld->actor_spec().id()) {
        return nullptr;
    }

    // Find a client that can simulate this actor
    auto pos = mBehaviorMap.find(actor.actor_spec().behavior_id());
    rrThrowVerify(pos != mBehaviorMap.end(),
                  "Fail to create " + actor.actor_spec().id() + " because behavior " + actor.actor_spec().behavior_id() + " is not valid.");
    std::string client_id = mPublisher->AllocateActorSimulator(*(pos->second), actor.actor_spec().id());

    // Create the actor by copying the actor specification
    rrSP<Actor> rtActor = std::make_shared<Actor>(actor);
    // Associate the actor with a simulator
    rtActor->mutable_actor_spec()->set_simulator_id(client_id);

    return rtActor;
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::SetRuntimeActors(const SetRuntimeActorsRequest& req, SetRuntimeActorsResponse&) {
    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    Result           result;
    std::unique_lock lock(ActorMutex);

    int num = req.actor_runtime_size();
    for (int i = 0; i < num; ++i) {
        const ActorRuntime& rtActor = req.actor_runtime(i);
        const std::string&  id      = rtActor.id();
        if (RtActors.find(id) == RtActors.end()) {
            result.AddError("Actor " + id + " does not exist");
        } else {
            rrSP<ActorRuntime> actorRuntime = std::make_shared<ActorRuntime>(rtActor);

            // xxx To do: change all simulation clients to set local_pose instead of pose
            actorRuntime->mutable_local_pose()->CopyFrom(rtActor.pose());
            auto pos = UnCommittedActors.insert({id, actorRuntime});
            if (!pos.second) {
                result.AddError("Actor " + actorRuntime->id() +
                                " has been updated more than once within one step");
            }
            // Set runtime actors API requires setting pose etc. in the world
            // coordinate system
            mWorldCoordSet.insert(id);
        }
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::SetActorPoses(const sseProto::SetActorPosesRequest& req, sseProto::SetActorPosesResponse&) {
    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    Result           result;
    std::unique_lock lock(ActorMutex);

    int num = req.actor_poses_size();
    for (int i = 0; i < num; ++i) {
        const auto& actorPose = req.actor_poses(i);
        auto        r         = UpdateActorPose(actorPose);
        if (r.first.Type == Result::EnumType::eError) {
            result.AddError(r.first.Message);
        }
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::SetVehiclePoses(const sseProto::SetVehiclePosesRequest& req, sseProto::SetVehiclePosesResponse&) {
    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    Result           result;
    std::unique_lock lock(ActorMutex);

    int num = req.vehicle_poses_size();
    for (int i = 0; i < num; ++i) {
        const auto& vehiclePose = req.vehicle_poses(i);
        const auto& actorPose   = vehiclePose.actor_pose();
        auto        r           = UpdateActorPose(actorPose);
        if (r.first.Type == Result::EnumType::eError) {
            result.AddError(r.first.Message);
        } else {
            // Additional operation for vehicle actor
            auto pos          = r.second;
            auto actorRuntime = pos->second;
            if (actorRuntime->has_vehicle_runtime()) {
                actorRuntime->mutable_vehicle_runtime()->mutable_wheels()->CopyFrom(vehiclePose.wheels());
            } else {
                result.AddError("Setting vehicle attributes on actor " + actorPose.actor_id() +
                                " is not allowed, because this actor is not a vehicle.");
            }
        }
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::CommitActorUpdates() {
    std::unique_lock lock(ActorMutex);

    for (auto& [id, actor] : UnCommittedActors) {
        RtActors[id]->mutable_actor_runtime()->CopyFrom(*actor);
    }

    // Update derived attributes of actors
    if (!UnCommittedActors.empty()) {
        UpdateActors(mWorld, nullptr, false);
    }

    PerformActorTypeSpecificUpdate();

    // Add the actor runtime state to actor runtime logging
    LogActorRuntimeState(UnCommittedActors);

    UnCommittedActors.clear();
    mWorldCoordSet.clear();
}

////////////////////////////////////////////////////////////////////////////////

std::pair<Result, std::unordered_map<std::string, rrSP<ActorRuntime>>::iterator>
Simulation::UpdateActorPose(const ActorPose& actorPose) {
    Result                                                        result;
    std::unordered_map<std::string, rrSP<ActorRuntime>>::iterator pos;

    const auto& id  = actorPose.actor_id();
    auto        ref = RtActors.find(id);
    if (ref == RtActors.end()) {
        result.AddError("Actor " + id + " does not exist");
    } else {
        rrSP<ActorRuntime> actorRuntime    = std::make_shared<ActorRuntime>(ref->second->actor_runtime());
        bool               worldCoordInUse = false;

        switch (actorPose.reference_frame()) {
        case sseProto::ReferenceFrame::REFERENCE_FRAME_WORLD:
            worldCoordInUse = true;
            actorRuntime->mutable_pose()->CopyFrom(actorPose.pose());
            actorRuntime->mutable_velocity()->CopyFrom(actorPose.velocity());
            actorRuntime->mutable_angular_velocity()->CopyFrom(actorPose.angular_velocity());
            break;
        case sseProto::ReferenceFrame::REFERENCE_FRAME_PARENT:
            actorRuntime->mutable_local_pose()->CopyFrom(actorPose.pose());
            actorRuntime->mutable_local_velocity()->CopyFrom(actorPose.velocity());
            actorRuntime->mutable_local_angular_velocity()->CopyFrom(actorPose.angular_velocity());
            break;
        /*case sseProto::ReferenceFrame::REFERENCE_FRAME_SPECIFIED: {
                // Convert the pose in specified coordinate system into the pose in
                // the world coordinate system
                worldCoordInUse = true;
                ActorPose worldPose;
                worldPose.set_reference_frame(sseProto::ReferenceFrame::REFERENCE_FRAME_SPECIFIED);
                worldPose.mutable_coordinate_system_ref()->mutable_coordinate_system()->
                        CopyFrom(mWorld->actor_spec().world_spec().coordinate_system());
                std::string errorMsg;
                auto retVal = Util::ConvertActorPose(actorPose, worldPose, &errorMsg);
                if (!retVal) {
                        result.AddError(errorMsg);
                }
                actorRuntime->mutable_pose()->CopyFrom(worldPose.pose());
                actorRuntime->mutable_velocity()->CopyFrom(worldPose.velocity());
                actorRuntime->mutable_angular_velocity()->CopyFrom(worldPose.angular_velocity());
                break;
                }*/
        default:
            result.AddError("Invalid reference frame type");
            return {result, pos};
        }

        auto r = UnCommittedActors.insert({id, actorRuntime});
        if (!r.second) {
            result.AddError("Actor " + actorRuntime->id() +
                            " has been updated more than once within one step");
        } else {
            pos = r.first;
            if (worldCoordInUse) {
                mWorldCoordSet.insert(id);
            }
        }
    }

    return {result, pos};
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::UpdateActors(rrSP<Actor> actor, rrSP<Actor> parent, bool parentChanged) {
    // First update the actor itself (the world actor does not need any update)
    bool isWorld  = (parent == nullptr);
    bool doUpdate = parentChanged;

    if (!isWorld) {
        const auto& id = actor->actor_spec().id();
        if (!doUpdate) {
            auto pos = UnCommittedActors.find(id);
            doUpdate = (pos != UnCommittedActors.end());
        }
        if (doUpdate) {
            bool worldCoordInUse = (mWorldCoordSet.find(id) != mWorldCoordSet.end());
            bool isChildOfWorld  = (parent == mWorld);
            if (worldCoordInUse) {
                // Use world pose to update the actor's local pose
                if (isChildOfWorld) {
                    auto rt = actor->mutable_actor_runtime();
                    rt->mutable_local_pose()->CopyFrom(rt->pose());
                    rt->mutable_local_velocity()->CopyFrom(rt->velocity());
                    rt->mutable_local_angular_velocity()->CopyFrom(rt->angular_velocity());
                }
                /*else {
                        Util::SetLocalCoordinates(*actor->mutable_actor_runtime(),
                                parent->actor_runtime());
                }*/
            } else {
                // Use local pose to update the actor's world pose
                if (isChildOfWorld) {
                    auto rt = actor->mutable_actor_runtime();
                    rt->mutable_pose()->CopyFrom(rt->local_pose());
                    rt->mutable_velocity()->CopyFrom(rt->local_velocity());
                    rt->mutable_angular_velocity()->CopyFrom(rt->local_angular_velocity());
                }
                /*else {
                        Util::SetWorldCoordinates(*actor->mutable_actor_runtime(),
                                parent->actor_runtime());
                }*/
            }
            // Update map locations if this is a vehicle actor
            // GetMapService().Update(actor);
        }
    }

    // Then update the actor's children
    int numChildren = actor->actor_runtime().children_size();
    for (int i = 0; i < numChildren; ++i) {
        auto childId  = actor->actor_runtime().children(i);
        auto childPos = RtActors.find(childId);
        if (childPos != RtActors.end()) {
            UpdateActors(childPos->second, actor, doUpdate);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::PerformActorTypeSpecificUpdate() {
    /*for (auto& [id, actor] : RtActors)
    {
            auto actorRuntime = actor->mutable_actor_runtime();
            if (!actorRuntime->has_character_runtime())
                    continue;

            auto characterRuntime = actorRuntime->mutable_character_runtime();
            if (characterRuntime->bones_size() != 0)
                    continue;

            auto& characterRt = mCharacterRuntimeMap.at(id);
            const auto& worldMatrix = rrConvert<glm::dmat4>(actorRuntime->pose().matrix());
            // Not 100% sure this step size is guaranteed to correspond to the most recent simulation step.
            const auto& localVelocity = actorRuntime->local_velocity();
            glm::vec3 localVel(localVelocity.x(), localVelocity.y(), localVelocity.z());
            characterRt->Update(mStepSize, glm::length(localVel), worldMatrix);

            const auto& worldBoneMatrices = characterRt->GetWorldMatrices();
            if (!worldBoneMatrices.empty())
            {
                    const auto& boneNames = characterRt->GetBoneNames();
                    for (size_t idx : rrMakeIndexRange(worldBoneMatrices))
                    {
                            const auto& mat = worldBoneMatrices[idx];
                            const auto& name = boneNames[idx];
                            auto bone = actorRuntime->mutable_character_runtime()->add_bones();
                            bone->set_name(name.toStdString());
                            *bone->mutable_transform()->mutable_matrix() = rrConvert<mathworks::scenario::common::Matrix4x4>((glm::dmat4)mat);
                    }
            }
    }*/
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::ClearCharacterBoneTransforms() {
    std::unique_lock lock(ActorMutex);

    for (auto& [id, actor] : RtActors) {
        VZ_UNUSED(id);

        auto actorRuntime = actor->mutable_actor_runtime();
        if (!actorRuntime->has_character_runtime()) {
            continue;
        }

        auto characterRuntime = actorRuntime->mutable_character_runtime();
        characterRuntime->clear_bones();
    }
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::ProcessEventForReplay(const rrSP<sseProto::Event>& evt) {
    /*if (GetIsReplayMode())
    {
            switch (evt->type_case()) {
            case sseProto::Event::kSimulationStepEvent:
                    mReplaySimBehavior->OnSimulationStep(evt->simulation_step_event());
                    break;
            case sseProto::Event::kSimulationStartEvent:
                    mReplaySimBehavior->OnSimulationStart(evt->simulation_start_event());
                    break;
            case sseProto::Event::kSimulationStopEvent:
                    mReplaySimBehavior->OnSimulationStop(evt->simulation_stop_event());
                    break;
            case sseProto::Event::kSimulationPostStepEvent:
                    mReplaySimBehavior->OnSimulationPostStep(evt->simulation_post_step_event());
                    break;
            default:
                    break;
            }
    }*/
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::CommitEventUpdates(CustomEventQueue::SenderType senderType) {
    std::unique_lock lock(mCustomEventMutex);

    mLastSender = senderType;

    // First clear up the expired custom events
    for (auto& evt : mCustomEventMap) {
        switch (senderType) {
        case CustomEventQueue::SenderType::eSupervisor:
            evt.second.mEventsFromSupervisor.clear();
            break;
        case CustomEventQueue::SenderType::eActors:
            evt.second.mEventsFromActors.clear();
            break;
        default:
            break;
        }
    }

    // Add new custom events to the specified event queue
    size_t eventCount = 0;
    for (const auto& [evtName, evts] : mUnCommittedCustomEvents) {
        eventCount += evts.size();
        switch (senderType) {
        case CustomEventQueue::SenderType::eSupervisor:
            mCustomEventMap[evtName].mEventsFromSupervisor.insert(
                mCustomEventMap[evtName].mEventsFromSupervisor.end(), evts.begin(), evts.end());
            break;
        case CustomEventQueue::SenderType::eActors:
            mCustomEventMap[evtName].mEventsFromActors.insert(
                mCustomEventMap[evtName].mEventsFromActors.end(), evts.begin(), evts.end());
            break;
        default:
            break;
        }
    }

    // Add the custom events to state logging
    std::set<rrSP<sseProto::Event>> eventsToBeLogged;
    for (const auto& customCommands : mUnCommittedCustomEvents) {
        eventsToBeLogged.insert(customCommands.second.begin(), customCommands.second.end());
    }

    LogSimulationEvent(eventsToBeLogged);

    mUnCommittedCustomEvents.clear();

    rrThrowVerify(eventCount <= INT_MAX, "Event queue overflow.");
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::GetRuntimeActors(const GetRuntimeActorsRequest&, GetRuntimeActorsResponse& res) {
    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    std::shared_lock lock(ActorMutex);

    for (const auto& actor : RtActors) {
        res.mutable_actor_runtime()->Add()->CopyFrom(actor.second->actor_runtime());
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::GetActors(const GetActorsRequest&, GetActorsResponse& res) {
    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    std::shared_lock lock(ActorMutex);

    for (const auto& actor : RtActors) {
        res.mutable_actors()->Add()->CopyFrom(*(actor.second));
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::GetActorPoses(const GetActorPosesRequest& req, GetActorPosesResponse& res) {
    // Validate inputs
    if (req.reference_frame() == ReferenceFrame::REFERENCE_FRAME_UNSPECIFIED) {
        return Result::Error("Simulation has not started");
    }

    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    Result           result;
    std::shared_lock lock(ActorMutex);

    for (const auto& actor : RtActors) {
        auto* actorPose = res.mutable_actor_poses()->Add();

        // Copy over the actor's pose, do conversion if necessary
        actorPose->set_actor_id(actor.first);
        actorPose->set_reference_frame(req.reference_frame());

        switch (req.reference_frame()) {
        case ReferenceFrame::REFERENCE_FRAME_WORLD:
            // Return the world pose
            actorPose->mutable_pose()->CopyFrom(actor.second->actor_runtime().pose());
            actorPose->mutable_velocity()->CopyFrom(actor.second->actor_runtime().velocity());
            actorPose->mutable_angular_velocity()->CopyFrom(actor.second->actor_runtime().angular_velocity());
            break;
        case ReferenceFrame::REFERENCE_FRAME_PARENT:
            // Return the local pose (i.e., pose in parent frame)
            actorPose->mutable_pose()->CopyFrom(actor.second->actor_runtime().local_pose());
            actorPose->mutable_velocity()->CopyFrom(actor.second->actor_runtime().local_velocity());
            actorPose->mutable_angular_velocity()->CopyFrom(actor.second->actor_runtime().local_angular_velocity());
            break;
        case ReferenceFrame::REFERENCE_FRAME_SPECIFIED: {
            // Convert the world pose into the specified coordinate system
            ActorPose worldPose;
            // worldPose object is used to store this actor's pose/velocity in the current world reference
            // frame. Doing this because currently only ActorRuntime objects are cached and not ActorPose objects
            // of actors.
            // Copying over actor parameters from ActorRuntime to constructed ActorPose object
            worldPose.set_reference_frame(ReferenceFrame::REFERENCE_FRAME_SPECIFIED);
            worldPose.mutable_coordinate_system_ref()->mutable_coordinate_system()->CopyFrom(mWorld->actor_spec().world_spec().coordinate_system());

            worldPose.mutable_pose()->CopyFrom(actor.second->actor_runtime().pose());
            worldPose.mutable_velocity()->CopyFrom(actor.second->actor_runtime().velocity());
            worldPose.mutable_angular_velocity()->CopyFrom(actor.second->actor_runtime().angular_velocity());

            actorPose->mutable_coordinate_system_ref()->CopyFrom(req.coordinate_system_ref());
            std::string errorMsg;
            auto        retVal = rrProtoGrpc::Util::ConvertActorPose(worldPose, *actorPose, &errorMsg);
            if (!retVal) {
                result.AddError(errorMsg);
            }
            break;
        }
        default:
            break;
        }
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::GetGeodeticCoordinate(const GetGeodeticCoordinateRequest& req, GetGeodeticCoordinateResponse& res) {
    auto      fromCoord = req.source_coordinate_system().coordinate_system();
    glm::vec3 ecefxyz(req.source_coordinates().x(), req.source_coordinates().y(), req.source_coordinates().z());
    glm::vec3 geodeticPose;
    Result    result;

    switch (fromCoord.type_case()) {
    case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kProjectedCoordinateSystem:
        return Result::Error("Conversions to/from projected coordinate system is not supported.");
        // geodeticPose = rrProtoGrpc::Util::ConvertCoordProjToGeog(ecefxyz, fromCoord.projected_coordinate_system().projection());
        // res.mutable_geodetic_coordinates()->set_latitude(geodeticPose.x);
        // res.mutable_geodetic_coordinates()->set_longitude(geodeticPose.y);
        // res.mutable_geodetic_coordinates()->set_altitude(geodeticPose.z);
        break;
    case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeographicCoordinateSystem:
        // Copy source coordinate to destination coordinate
        res.mutable_geodetic_coordinates()->set_latitude(req.source_coordinates().x());
        res.mutable_geodetic_coordinates()->set_longitude(req.source_coordinates().y());
        res.mutable_geodetic_coordinates()->set_altitude(req.source_coordinates().z());
        break;
    case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kGeocentricCoordinateSystem:
        geodeticPose = rrProtoGrpc::Util::ConvertCoordEcefToGeog(ecefxyz);
        res.mutable_geodetic_coordinates()->set_latitude(geodeticPose.x);
        res.mutable_geodetic_coordinates()->set_longitude(geodeticPose.y);
        res.mutable_geodetic_coordinates()->set_altitude(geodeticPose.z);
        break;
    case sseProtoCommon::CoordinateReferenceSystem::TypeCase::kEngineeringCoordinateSystem: {
        glm::vec3 geodeticOrigin(fromCoord.engineering_coordinate_system().geodetic_origin().latitude(), fromCoord.engineering_coordinate_system().geodetic_origin().longitude(),
                                 fromCoord.engineering_coordinate_system().geodetic_origin().altitude());
        geodeticPose = rrProtoGrpc::Util::ConvertCoordEngToGeog(ecefxyz, geodeticOrigin);
        res.mutable_geodetic_coordinates()->set_latitude(geodeticPose.x);
        res.mutable_geodetic_coordinates()->set_longitude(geodeticPose.y);
        res.mutable_geodetic_coordinates()->set_altitude(geodeticPose.z);
    } break;
    default:
        break;
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

std::vector<rrSP<sseProto::Actor>> Simulation::GetActorsInternal() const {
    std::vector<rrSP<sseProto::Actor>> retVal;
    std::shared_lock                   lock(ActorMutex);

    for (auto& idActor : RtActors) {
        retVal.push_back(idActor.second);
    }
    return retVal;
}

////////////////////////////////////////////////////////////////////////////////

rrSP<Actor> Simulation::FindActor(const std::string& actorId, bool expectExists) const {
    std::shared_lock lock(ActorMutex);

    if (actorId == cWorldActorID) {
        return mRtWorldActor;
    }

    auto pos = RtActors.find(actorId);
    if (pos != RtActors.end()) {
        return pos->second;
    } else if (expectExists) {
        rrThrow("Actor " + actorId + " does not exist.");
    }
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////

const Behavior* Simulation::FindBehavior(const std::string& actorId, bool expectExists) const {
    std::shared_lock lock(ActorMutex);

    auto pos = mBehaviorMap.find(actorId);
    if (pos != mBehaviorMap.end()) {
        return pos->second;
    } else if (expectExists) {
        rrThrow("Actor behavior " + actorId + " does not exist.");
    }
    return nullptr;
}

////////////////////////////////////////////////////////////////////////////////

bool Simulation::IsParentOrAncestor(const ActorRuntime& ancestor, const ActorRuntime& child) const {
    const auto& ancestorId = ancestor.id();
    std::string parentId   = child.parent();
    while (parentId != ancestorId && parentId != "0") {
        auto actor = FindActor(parentId);
        parentId   = actor->actor_runtime().parent();
    }
    return (parentId == ancestorId);
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::NotifyActionComplete(const NotifyActionCompleteRequest& req,
                                        NotifyActionCompleteResponse&) {
    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    mScenario->NotifyActionComplete(req.action_id());
    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::SendEvents(const SendEventsRequest& req, SendEventsResponse&) {
    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    Result           result;
    std::unique_lock lock(mCustomEventMutex);

    int num = req.events_size();
    for (int i = 0; i < num; ++i) {
        const auto& event = req.events(i);
        if (!event.has_user_defined_event()) {
            return Result::Error("Encountered invalid event type. SendEvent requires user-defined events.");
        }

        const auto& customCommand = event.user_defined_event();
        const auto& eventName     = customCommand.name();

        try {
            rrThrowVerify(mCustomEventDefinitionMap.find(eventName) != mCustomEventDefinitionMap.end(),
                          "Undefined custom event name " + eventName + ". Define this event by creating a user-defined event asset in <PROJECT>/Events.");

            // Validate the to-be-sent custom event
            mCustomEventDefinitionMap[eventName].ValidateEvent(customCommand);

            // Successfull validation, proceed to cache the event
            mUnCommittedCustomEvents[eventName].insert(std::make_shared<Event>(event));
        } catch (const rrException& e) {
            result.AddError("Encountered error during send event action." + e.ToString());
        }
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::ReceiveEvents(const ReceiveEventsRequest& req, ReceiveEventsResponse& res) {
    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    auto addEvents = [](ReceiveEventsResponse& res, const std::vector<rrSP<Event>>& evts) {
        for (const auto& evt : evts) {
            res.add_events()->CopyFrom(*evt);
        }
    };

    auto processQueue = [this, &addEvents](ReceiveEventsResponse& res, const CustomEventQueue& q) {
        // Check who are the last custom event senders so that FIFO is ensured in the return values
        if (mLastSender == CustomEventQueue::SenderType::eSupervisor) {
            addEvents(res, q.mEventsFromActors);
            addEvents(res, q.mEventsFromSupervisor);
        } else {
            addEvents(res, q.mEventsFromSupervisor);
            addEvents(res, q.mEventsFromActors);
        }
    };

    Result           result;
    std::shared_lock lock(mCustomEventMutex);

    int num = req.event_names_size();
    if (num == 0) {
        // Request for all the custom events in the last step
        for (const auto& evt : mCustomEventMap) {
            processQueue(res, evt.second);
        }
    } else {
        // Request for the custom events with the specified names
        for (int i = 0; i < num; ++i) {
            auto pos = mCustomEventMap.find(req.event_names(i));
            if (pos == mCustomEventMap.end()) {
                result.AddError("");
            } else {
                processQueue(res, pos->second);
            }
        }
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::GetPhaseStatus(const sseProto::GetPhaseStatusRequest&, sseProto::GetPhaseStatusResponse& res) {
    // Early return if simulation is not running
    if (!IsSimulationRunning()) {
        return Result::Error("Simulation has not started");
    }

    auto* phaseExecutor = dynamic_cast<PhaseDiagramExecutor*>(mScenario.get());
    if (phaseExecutor) {
        phaseExecutor->GetPhaseStatus(res);
    }

    return Result::Success();
}

////////////////////////////////////////////////////////////////////////////////

Result Simulation::SimulationPacer::SetSimulationPace(bool isOn, double pace) {
    // Set pacer on/off status
    IsPacerOn = isOn;

    // Validate and set the simulation pace
    if (pace < cLowerBound || pace > cUpperBound) {
        return Result::Error("Simulation pace must be greater than or equal to " +
                             std::to_string(cLowerBound) + " and less than or equal to " + std::to_string(cUpperBound));
    } else {
        SimulationPace = pace;
        return Result::Success();
    }
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::SimulationPacer::RecordStartTime() {
    if (SimulationPace.load() > 0.0) {
        StartTime = std::chrono::system_clock::now();
    }
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::SimulationPacer::ApplyPacing(double elapsedSimTime) {
    if (IsPacerOn) {
        auto                          pacingValue          = SimulationPace.load();
        std::chrono::duration<double> elapsedWallClockTime = std::chrono::system_clock::now() - StartTime;
        // SimulationPace = (elapsed simulation time)/(elapsed wall clock time)
        std::chrono::duration<double> requiredWallClockTime = std::chrono::duration<double>(elapsedSimTime / pacingValue);

        // reset the condition
        if (elapsedWallClockTime < requiredWallClockTime) {
            // Wait for simulation to finish running
            std::unique_lock ulock(PacingLock);
            ContinueSleeping = false;
            AbortPacingCV.wait_for(ulock, (requiredWallClockTime - elapsedWallClockTime),
                                   [this] { return ContinueSleeping; });
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::SimulationPacer::BreakFromPacing() {
    std::lock_guard lock(PacingLock);
    ContinueSleeping = true;
    AbortPacingCV.notify_one();
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::NotifySimStatusChanged(SimulationStatus status) const {
    rrSP<Event> evt = EventCalendar::CreateSimulationStatusChangedEvent(status);
    PublishEvent(evt);

    auto* simServer = mPublisher->GetSimulationServer();
    if (simServer) {
        simServer->NotifySimStatusChanged(mSimulationStatus);
    }
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::LogActorRuntimeState(std::unordered_map<std::string, rrSP<sseProto::ActorRuntime>>& actorRtState) {
    // Add actor runtime states. For now we ignore the return value of the call.
    if (mActorRtLogger) {
        mActorRtLogger->LogActorRunTimeStates(actorRtState);
    }
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::LogSimulationEvent(const std::set<rrSP<sseProto::Event>>& userDefinedEvents) {
    if (mActorRtLogger) {
        for (const auto& evt : userDefinedEvents) {
            mActorRtLogger->LogSimulationEvent(*evt);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::SetActorRuntimeLogger(ActorRunTimeLogger* pActorRtLogger) {
    mActorRtLogger = pActorRtLogger;
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::SetDiagnosticMessageLogger(DiagnosticMessageLogger* pDiagnosticMsgLogger) {
    mDiagnosticMsgLogger = pDiagnosticMsgLogger;
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::LogEventToDiagnostics(rrSP<sseProto::Event> event) {
    if (event->has_simulation_stop_event()) {
        sseProto::DiagnosticType msgType = sseProto::INFORMATION_TYPE;

        if (event->simulation_stop_event().cause().has_simulation_failed()) {
            msgType = sseProto::ERROR_TYPE;
        } else if (event->simulation_stop_event().cause().has_simulation_complete()) {
            msgType = sseProto::INFORMATION_TYPE;
        }

        LogDiagnostic(event->simulation_stop_event().cause().summary(), msgType,
                      event->simulation_stop_event().stop_time_seconds());
    }
}

////////////////////////////////////////////////////////////////////////////////

void Simulation::LogDiagnostic(const std::string& message, sseProto::DiagnosticType diagType, double simTime) {
    // Report diagnostics in non-replay mode.
    // In replay mode, the in-built replay behavior replays & reports diagnostics

    /*if (!mReplaySimBehavior) */
    {
        sseProto::DiagnosticMessage msgToAdd;

        msgToAdd.set_diagnostic_message(message);
        msgToAdd.set_diagnostic_type(diagType);
        msgToAdd.mutable_time_stamp()->set_simulation_time(simTime);

        sseProto::AddDiagnosticMessageRequest  req;
        sseProto::AddDiagnosticMessageResponse res;
        req.add_diagnostic_messages()->CopyFrom(msgToAdd);

        if (mDiagnosticMsgLogger) {
            mDiagnosticMsgLogger->AddDiagnosticMessages(req, res);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
} // namespace sse
