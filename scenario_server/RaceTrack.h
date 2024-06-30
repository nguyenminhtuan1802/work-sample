#pragma once

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "RoadRunnerConversions.h"

#include "mathworks/scenario/simulation/scenario.pb.h"

#include <boost/graph/depth_first_search.hpp>

namespace sse {
class Phase;
class MixPhase;
class CompositePhase;
class ScenarioExecutor;

class SortingGroup {
  public:
    // Use a pair to represent a dependency between two phases, with the
    // first phase in the pair depends on the second phase
    typedef std::pair<Phase*, Phase*> Dependency;

    SortingGroup() {}

    SortingGroup(const std::vector<rrSP<Phase>>& children)
        : mChildren(&children) {
        for (size_t i = 0; i < children.size(); ++i) {
            mChildIdxMap.insert({children[i].get(), i});
        }
    }

    void AddDependency(const Dependency& actualDep, const Dependency& execDep) {
        mActualDeps.push_back(actualDep);
        mExecDeps.push_back(execDep);
    }

    // Compute execution order. Based on execution dependencies among all the
    // members in this group, find an execution sequence that can satisfy all
    // the dependencies.
    std::vector<size_t> SortChildren();

  private:
    // Class CycleDetector is a custom Boost Depth-First-Search visitor that
    // helps detect cycles in a Boost directed graph.
    class CycleDetector : public boost::dfs_visitor<> {
      public:
        CycleDetector(bool& hasCycle, std::pair<size_t, size_t>& edge)
            : mHasCycle(hasCycle)
            , mEdge(edge) {}

        template <class Edge, class Graph>
        void back_edge(Edge e, Graph&) {
            mHasCycle = true;
            mEdge     = {e.m_source, e.m_target};
        }

      private:
        // Whether the graph contains a cycle
        bool& mHasCycle;
        // An edge within the cycle (if a cycle exists)
        std::pair<size_t, size_t>& mEdge;
    };

    // A reference to members in this sorting group. These members are
    // corresponding with children of a composite phase
    const std::vector<rrSP<Phase>>* mChildren = nullptr;

    // Phases in this sorting group, each maps to an index that is
    // corresponding with a phase' position in its parent's data model
    std::unordered_map<Phase*, int> mChildIdxMap;

    // Actual dependencies. An actual dependency may be established by
    // offspring of the sorting members
    std::vector<Dependency> mActualDeps;

    // Execution dependencies. Elements in this vector maps one-to-one to
    // the elements of the actual dependencies vector. An execution
    // dependency suggests to satisfy an actual dependency, what is the
    // resulting dependency on members within this sorting group.
    std::vector<Dependency> mExecDeps;
};

////////////////////////////////////////////////////////////////////////////////
// Class RaceTrack helps parallel phases to find a feasible execution order
// based on dependency analysis.
//
class RaceTrack {
  public:
    RaceTrack(ScenarioExecutor* const executor, MixPhase* starter)
        : mExecutor(executor)
        , mStarter(starter) {}

    // Generate execution order for descendants of a parallel phase
    void Sort();

  private:
    typedef std::unordered_map<Phase*, std::vector<Phase*>>   DependencyMap;
    typedef std::unordered_map<CompositePhase*, SortingGroup> SortingMap;

    // Given two offspring phases, find the two ancestor phases that have a
    // common parent (i.e. they are siblings with each other).
    //
    // For example, in:
    // P1: do parallel
    //     P2: do parallel
    //         P3: car1.drive()
    //         P4: car2.drive()
    //     P5: do parallel
    //         P6: car3.drive()
    //         P7: car4.drive()
    //
    // FindSiblingAncestors(P3, P6) shall return a tuple { P1, P2, P5 }, where
    // - First element: the common parent phase of the two sibling phases
    // - 2nd and 3rd elements: ancestors of specified offspring who are sibling
    //   with each other
    std::tuple<Phase*, Phase*, Phase*> FindSiblingAncestors(
        Phase* offspring1,
        Phase* offspring2) const;

  private:
    ScenarioExecutor* const                 mExecutor = nullptr;
    MixPhase*                               mStarter;
    std::unordered_map<std::string, Phase*> mRacers;
    DependencyMap                           mMemberDeps;
    SortingMap                              mSortingMap;
};
} // namespace sse
