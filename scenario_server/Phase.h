#pragma once

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "RoadRunnerConversions.h"

#include "mathworks/scenario/simulation/event.pb.h"
#include "mathworks/scenario/simulation/scenario.pb.h"

namespace sse {
class PhaseDiagramExecutor;
class Action;
class Condition;

////////////////////////////////////////////////////////////////////////////////
// Class Phase is the base class of all the phase objects. It implements the
// shared properties and interfaces.
//
class Phase {
    VZ_DECLARE_NONCOPYABLE(Phase);

  public:
    ////////////////////////////////////////////////////////////////////////////
    // Run-time state of this phase
    // - eIdle: the phase has not yet been executed
    // - eStart: the phase is starting
    // - eRun: all start conditions are satisfied, the phase is running
    // - eEnd: the phase has finished execution
    // Note that a phase can stay in 'eStart' or 'eEnd' phase for zero or more
    // simulation steps.
    //
    enum class State {
        eIdle = 0,
        eStart,
        eRun,
        eEnd
    };

    // Phase factory
    static rrSP<Phase> CreatePhase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto);

    virtual ~Phase() {}

    // Get data model
    const sseProto::Phase& GetDataModel() const {
        return mDataModel;
    }

    // Get and set parent
    Phase* GetParent() const {
        return mParent;
    }

    void SetParent(Phase* parent) {
        mParent = parent;
    }

    // Get Start condition
    rrSP<Condition> GetStartCondition() const {
        return mStartCondition;
    }

    // Get End condition
    rrSP<Condition> GetEndCondition() const {
        return mEndCondition;
    }

    // Update method promotes the state of this phase by evaluating state
    // transition conditions at run-time. This method allows state transitions
    // to occur multiple times within a single step.
    bool Update();

    Phase::State GetPhaseState() const {
        return mState;
    }

    // Return all the actors that this phase will directly depend on, in order
    // to start and run, e.g. 'car1: drive() with speed(3mps, faster than: car2)'
    // establishes a dependency to 'car2'. Note:
    // - The actor of this phase is not considered such a dependency.
    // - The dependencies in end triggers of a phase are not considered by this call.
    virtual void GetDependencyToRun(std::vector<std::string>& deps) const;

    // Initialize any actor related to this phase by enforcing all the initial
    // actions
    virtual void InitializeActor(sseProto::Actor&) const {}

    // Call backs to transit phase state and do state entering actions
    virtual void OnStart();
    virtual void OnRun();
    virtual void OnEnd();

    // Call backs to do temporal update and check value conditions. The return
    // value indicates whether phase state transition shall happen after
    // evaluating this call back.
    virtual bool DoStartUpdate();
    virtual bool DoRunUpdate();

    virtual void GetPhaseStatus(sseProto::PhaseStatus* rtPhase);

    // For an active phase, return all its children that are also active
    virtual std::vector<rrSP<Phase>> GetActiveChildren() const {
        return {};
    }

    // Assuming executing the phase now, return any actor the phase will
    // directly create
    virtual rrOpt<std::string> HasActorCreation() const {
        return {};
    }

    // Have all the actions of this phase completed
    virtual bool HasCompleted() const = 0;

  protected:
    // Update actions of a phase during DoRunUpdate()
    virtual void UpdatePhaseStatus() {}

  protected:
    Phase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto);

    // Run-time scenario this phase belongs to
    PhaseDiagramExecutor* const mExecutor = nullptr;

    // SSD data model of this phase
    const sseProto::Phase& mDataModel;

    // Parent of this phase
    // - In 'P0: do parallel: P1: car1.drive(); P2: car2.drive()', P0 is the
    //   parent of both P1 and P2
    // - Parent of a root phase is null
    Phase* mParent = nullptr;

    // Start and end conditions
    rrSP<Condition> mStartCondition;
    rrSP<Condition> mEndCondition;

    // Zero-time operations
    std::vector<rrSP<Phase>> mSystemActions;

    // Run-time state of this phase
    State mState = State::eIdle;

    // A flag to indicate whether there are initial actions to process
    bool mIsInitPending = false;

    // Declare friend class for testing
    friend class ScenarioLogicTestFixture;
};

////////////////////////////////////////////////////////////////////////////////
// Class CompositePhase is the parent class of all the phases that are consist
// of the execution of more than one phases, such as: do parallel(),
// do serial(), do mix(), do one_of() etc.
//
class CompositePhase : public Phase {
  public:
    const std::vector<rrSP<Phase>>& GetChildren() const {
        return mChildren;
    }

    void SetExecOrder(const std::vector<size_t>& execOrder) {
        mExecOrder = execOrder;
    }

    bool HasCompleted() const override {
        return mChildrenDone;
    }

  protected:
    CompositePhase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto);
    std::vector<rrSP<Phase>> mChildren;
    std::vector<size_t>      mExecOrder;
    bool                     mChildrenDone = false;
};

////////////////////////////////////////////////////////////////////////////////
// Class MixPhase implements a do parallel() phase that contains multiple
// phases that are executing at the same time.
//
class MixPhase : public CompositePhase {
  public:
    MixPhase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto)
        : CompositePhase(executor, proto) {
        rrThrowVerify(proto.composite_phase().has_mix_phase(), "Invalid data model for a parallel phase.");
    }

    void OnEnd() override;
    bool DoRunUpdate() override;

    std::vector<rrSP<Phase>> GetActiveChildren() const override {
        return mChildren;
    }
};

////////////////////////////////////////////////////////////////////////////////
// Class SerialPhase implements a do serial() phase that contains multiple
// phases that execute one after the other.
//
class SerialPhase : public CompositePhase {
  public:
    SerialPhase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto)
        : CompositePhase(executor, proto) {
        rrThrowVerify(proto.composite_phase().has_serial_phase(), "Invalid data model for a serial phase.");
    }

    void OnEnd() override;
    bool DoRunUpdate() override;

    std::vector<rrSP<Phase>> GetActiveChildren() const override {
        return {mChildren[0]};
    }

  private:
    size_t mPosition = 0;
};

////////////////////////////////////////////////////////////////////////////////
// Class ActionPhase implements a phase that is regarding a single action or
// operation.
//
class ActionPhase : public Phase {
  public:
    void         GetDependencyToRun(std::vector<std::string>& deps) const override;
    void         InitializeActor(sseProto::Actor& actor) const override;
    void         OnRun() override;
    void         OnEnd() override;
    bool         DoRunUpdate() override;
    void         GetPhaseStatus(sseProto::PhaseStatus* rtPhase) override;
    rrSP<Action> FindFinalAction(const std::string& actionId) const;
    bool         HasCompleted() const override;

  protected:
    ActionPhase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto)
        : Phase(executor, proto) {
        rrThrowVerify(proto.has_action_phase(), "Invalid data model for a singular phase.");
    }

    void UpdatePhaseStatus() override;

    const std::vector<rrSP<Action>>& GetFinalActions() const {
        return mFinalActions;
    }

    void DispatchFinalActions() const;

  protected:
    // Actor actions at start of this phase
    std::vector<rrSP<Action>> mInitialActions;
    // Actor actions at end of this phase
    std::vector<rrSP<Action>> mFinalActions;
    // Actor actions during this phase
    std::vector<rrSP<Action>> mInvariantActions;
};

////////////////////////////////////////////////////////////////////////////////
// Class ActorActionPhase implements a phase that regards to an action that an
// actor takes. An action phase must be associated with an actor.
//
class ActorActionPhase : public ActionPhase {
  public:
    ActorActionPhase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto);
    void               OnRun() override;
    void               OnEnd() override;
    rrOpt<std::string> HasActorCreation() const override;
    // Detect and resolve conflict with another action phase
    void DetectAndResolveConflict(const ActorActionPhase* newPhase) const;
    // Remove any skipped actions from the original phase data model
    sseProto::Phase GetUnskippedActions() const;
    // Respond to notification that an action event has been dispatched
    void NotifyActionEventDispatched() const;

  protected:
    std::string mActorId;
};

////////////////////////////////////////////////////////////////////////////////
// Class SystemAction implements a zero-time system operation in a scenario.
//
// Zero-time operations include:
// - ScenarioSuccess: operation to pass and end a scenario test
// - ScenarioFailure: operation to fail and end a scenario test
//
// Many zero-time operations are conditional, e.g., with syntax such as:
//   on @condition
//       ScenarioFailure()
//
class SystemActionPhase : public ActionPhase {
  public:
    SystemActionPhase(PhaseDiagramExecutor* executor, const sseProto::Phase& proto);
    void OnRun() override;
};
} // namespace sse
