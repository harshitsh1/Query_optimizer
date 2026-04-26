#ifndef HEURISTIC_OPTIMIZER_H
#define HEURISTIC_OPTIMIZER_H

#include <memory>
#include <vector>
#include "logical_plan.h"
#include "catalog.h"

using namespace std;

// Heuristic (Rule-Based) Optimizer

class HeuristicOptimizer {
public:
    explicit HeuristicOptimizer(const Catalog& catalog);

    // Apply all heuristic optimizations and return a new optimized tree
    shared_ptr<LogicalNode> optimize(shared_ptr<LogicalNode> plan);

private:
    const Catalog& catalog_;

    // Decompose conjunctions and push predicates as close to
    // base relations as possible
    shared_ptr<LogicalNode> pushdownSelections(shared_ptr<LogicalNode> node);

    // Collect all atomic predicates from a predicate tree
    void flattenConjunction(const Predicate& pred, vector<Predicate>& out);

    // Rebuild a conjunction from a list of predicates
    Predicate buildConjunction(const vector<Predicate>& preds);

    // Try to push a single predicate into a subtree
    shared_ptr<LogicalNode> pushPredicate(shared_ptr<LogicalNode> node, const Predicate& pred);

    //Projection Pushdown
    shared_ptr<LogicalNode> pushdownProjections(shared_ptr<LogicalNode> node);

    //Join Commutativity Swap join inputs so smaller relation is on the left
    shared_ptr<LogicalNode> reorderJoins(shared_ptr<LogicalNode> node);

    // Estimate row count for a node 
    int estimateRows(shared_ptr<LogicalNode> node);
};

#endif
