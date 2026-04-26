#ifndef PLAN_ENUMERATOR_H
#define PLAN_ENUMERATOR_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <set>
#include "logical_plan.h"
#include "physical_plan.h"
#include "cost_estimator.h"
#include "catalog.h"

using namespace std;

struct PlanCandidate {
    string description;
    double cost;
    shared_ptr<PhysicalNode> plan;
};

class PlanEnumerator {
public:
    PlanEnumerator(const Catalog& catalog, const CostEstimator& estimator);

    // Enumerate plans for the given logical plan tree Returns the best physical plan and populates allPlans for UI
    shared_ptr<PhysicalNode> findBestPlan(
        shared_ptr<LogicalNode> logicalPlan,
        vector<PlanCandidate>& allPlans);

private:
    const Catalog& catalog_;
    const CostEstimator& estimator_;

    struct DPEntry {
        shared_ptr<PhysicalNode> plan;
        double cost = 1e18;
        int estimatedRows = 0;
    };
    unordered_map<int, DPEntry> dpTable_;

    struct RelationInfo {
        string tableName;
        string alias;
        int index;          // bit position in bitmask
        vector<Predicate> localPredicates;  // single-table WHERE predicates
    };
    vector<RelationInfo> relations_;

    // Join predicates
    struct JoinPredInfo {
        Predicate pred;
        set<string> tables;   // tables referenced (string-based fallback)
        int leftMask  = 0;    // bitmask of relation indices on left side of this JOIN node
        int rightMask = 0;    // bitmask of relation indices on right side of this JOIN node
        // Exact bits of the specific relations referenced by each operand of the predicate.
        // These are determined by searching within the correct subtree index range, so they
        // correctly disambiguate duplicate table names (e.g., four subquery scans aliased 'b').
        int jp_leftRefBit  = -1;  // relation index referenced by left operand (-1 = unknown)
        int jp_rightRefBit = -1;  // relation index referenced by right operand (-1 = unknown)
    };
    vector<JoinPredInfo> joinPredicates_;

    // Top-level projection columns
    vector<ColumnRef> projectionCols_;

    void extractRelationsAndPredicates(shared_ptr<LogicalNode> node);
    void extractFromNode(shared_ptr<LogicalNode> node);
    void classifyPredicate(const Predicate& pred);

    //DP core 
    DPEntry findBest(int subset);

    // Single-relation access plan
    DPEntry singleRelationPlan(int relIdx);

    // Find applicable join predicates for a subset
    vector<Predicate> findJoinPredicates(int leftMask, int rightMask);

    // Utility: get table name for a relation index
    string getTableName(int idx) const;
    string getEffectiveName(int idx) const;

    // Bit manipulation helpers
    int relationBit(const string& tableName) const;
};

#endif 
