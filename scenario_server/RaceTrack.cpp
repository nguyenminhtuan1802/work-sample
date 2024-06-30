#include "Action.h"
#include "Phase.h"
#include "RaceTrack.h"
#include "Simulation.h"
#include "Utility.h"

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/properties.hpp>
#include <boost/graph/topological_sort.hpp>

using namespace sseProto;

namespace sse {
////////////////////////////////////////////////////////////////////////////////
// Generate execution order for descendants of a parallel phase.
//
// The procedure includes the following steps:
// - Trace from the starter parallel phase, find all the member phases on this
//   race track. Members include all the action phases that can execute in
//   parallel within this parallel phase, and the generations in-between.
// - Among the members, identify all the racer phases. A racer is a drive phase
//   that results in an actor creation/initialization at runtime.
// - Identify causal dependencies among members. For example, if initial drive
//   action of actor A is to match the speed of actor B, there is a dependency
//   to B. Therefore racer B must starts before racer A.
// - Identify execution orders among members. Based on causal dependencies,
//   across the nested generations, generate an execution order among children
//   of each parallel phase.
// - When computing the execution orders, any circular dependency will be
//   identified and reported as an error. Such an error maps to a user's
//   modeling mistake.
//
void RaceTrack::Sort() {
    // Find all members and identify racer member (a racer is a drive phase
    // that invokes actor creation/initialization)
    std::vector<Phase*> candidates;
    candidates.push_back(mStarter);
    while (!candidates.empty()) {
        // Add this phase to the member list
        auto curr = candidates.back();
        candidates.pop_back();

        if (curr != mStarter) {
            // Add this phase as an offspring to sort
            mMemberDeps.insert({curr, {}});

            // Check if the phase directly invokes actor creation
            rrOpt<std::string> newActor = curr->HasActorCreation();
            if (newActor.has_value()) {
                mRacers.insert({newActor.value(), curr});
            }
        }

        // Additional processing for composite member
        const auto& dataModel = curr->GetDataModel();
        if (dataModel.has_composite_phase()) {
            // Add the phase' active children to candidate list
            std::vector<rrSP<Phase>> children = curr->GetActiveChildren();
            for (auto& child : children) {
                candidates.push_back(child.get());
            }
            // Create a sorted group for a parallel phase and its children
            if (dataModel.composite_phase().has_mix_phase()) {
                MixPhase*   parPhase    = dynamic_cast<MixPhase*>(curr);
                const auto& parChildren = parPhase->GetChildren();
                mSortingMap.insert({parPhase, SortingGroup(parChildren)});
            }
        }
    }

    // Identify causal dependencies to racers
    for (auto& iter : mMemberDeps) {
        std::vector<std::string> dependencies;
        iter.first->GetDependencyToRun(dependencies);
        for (const auto& id : dependencies) {
            auto racer = mRacers.find(id);
            if (racer != mRacers.end()) {
                Phase* other = racer->second;
                if (iter.first != other) {
                    iter.second.push_back(other);
                }
            }
        }
    }

    // Find composite phases that can achieve the required execution orders
    for (auto& iter : mMemberDeps) {
        for (const auto& dep : iter.second) {
            std::tuple<Phase*, Phase*, Phase*> sortInfo =
                FindSiblingAncestors(iter.first, dep);
            CompositePhase* compPhase = dynamic_cast<CompositePhase*>(
                std::get<0>(sortInfo));
            rrVerify(compPhase != 0);
            Phase* phase1 = std::get<1>(sortInfo);
            Phase* phase2 = std::get<2>(sortInfo);
            mSortingMap[compPhase].AddDependency({iter.first, dep}, {phase1, phase2});
        }
    }
    for (auto& iter : mSortingMap) {
        auto execOrder = iter.second.SortChildren();
        iter.first->SetExecOrder(execOrder);
    }
}

////////////////////////////////////////////////////////////////////////////////

std::tuple<Phase*, Phase*, Phase*> RaceTrack::FindSiblingAncestors(
    Phase* offspring1,
    Phase* offspring2) const {
    // Generate a list from an offspring to the origin
    auto addAncestors = [&](Phase* offspring, std::vector<Phase*>& ancestors) {
        Phase* curr = offspring;
        ancestors.push_back(curr);
        Phase* parent;
        do {
            parent = curr->GetParent();
            ancestors.push_back(parent);
            curr = parent;
        } while (curr != mStarter);
    };

    // Get ancestors of offspring1
    std::vector<Phase*> ancestors1;
    addAncestors(offspring1, ancestors1);

    // Get ancestors of offspring2
    std::vector<Phase*> ancestors2;
    addAncestors(offspring2, ancestors2);

    // Find the two ancestors of two offspring who are siblings with each other
    Phase*                                parentPhase = nullptr;
    std::vector<Phase*>::reverse_iterator iter1       = ancestors1.rbegin();
    std::vector<Phase*>::reverse_iterator iter2       = ancestors2.rbegin();
    rrVerify(*iter1 == *iter2);
    while ((iter1 != ancestors1.rend()) &&
           (iter2 != ancestors2.rend()) &&
           (*iter1 == *iter2)) {
        parentPhase = *iter1;
        ++iter1;
        ++iter2;
    }
    rrThrowVerify((iter1 != ancestors1.rend()) && (iter2 != ancestors2.rend()),
                  "Unexpected error");
    return {parentPhase, *iter1, *iter2};
}

////////////////////////////////////////////////////////////////////////////////
// SortingGroup
////////////////////////////////////////////////////////////////////////////////

std::vector<size_t> SortingGroup::SortChildren() {
    typedef boost::adjacency_list<>                       Graph;
    typedef boost::graph_traits<Graph>::vertex_descriptor VertexIdType;
    typedef std::vector<VertexIdType>                     VertexIdVector;

    // Create a boost graph. The graph has the same number of vertices as
    // the number of members in this sorting group
    Graph graph(mChildIdxMap.size());

    // Add edges. Each edge presents a dependency
    for (const Dependency& dep : mExecDeps) {
        boost::add_edge(mChildIdxMap[dep.first], mChildIdxMap[dep.second], graph);
    }

    // Detect circular dependencies. Report an error if found
    bool                      hasCycle = false;
    std::pair<size_t, size_t> edgeInCycle;
    CycleDetector             cycleDetector(hasCycle, edgeInCycle);
    boost::depth_first_search(graph, boost::visitor(cycleDetector));
    if (hasCycle) {
        // Reports an error if there is a circular dependency for actor creation
        Phase* p1     = (*mChildren)[edgeInCycle.first].get();
        Phase* p2     = (*mChildren)[edgeInCycle.second].get();
        Phase* parent = p1->GetParent();
        for (size_t i = 0; i < mExecDeps.size(); ++i) {
            const auto& dep = mExecDeps[i];
            if (dep.first == p1 && dep.second == p2) {
                p1 = mActualDeps[i].first;
                p2 = mActualDeps[i].second;
                break;
            }
        }
        rrThrow("Invalid circular dependency when executing " + parent->GetDataModel().id() + ". " +
                p1->GetDataModel().id() + " and " + p2->GetDataModel().id() + " are in this cycle.");
    }

    // Perform topological sorting to identify a traverse of this directional graph
    VertexIdVector sortedResult;
    boost::topological_sort(graph, std::back_inserter(sortedResult));

    return sortedResult;
}

////////////////////////////////////////////////////////////////////////////////
} // namespace sse
