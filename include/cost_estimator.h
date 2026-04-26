#ifndef COST_ESTIMATOR_H
#define COST_ESTIMATOR_H

#include <memory>
#include "logical_plan.h"
#include "physical_plan.h"
#include "catalog.h"

using namespace std;


class CostEstimator {
public:
    explicit CostEstimator(const Catalog& catalog,
                           double tT = 0.1,    // block transfer time (ms)
                           double tS = 4.0);   // seek time (ms)


    double estimateSelectivity(const Predicate& pred, const string& tableName) const;


    int estimateJoinSize(const string& leftTable, int leftRows,
                         const string& rightTable, int rightRows,
                         const Predicate& joinCond) const;

    // Returns best physical scan node for a table with optional predicate
    shared_ptr<PhysicalNode> bestScanPlan(const string& tableName,
                                          int currentRows,
                                          const Predicate* pred = nullptr) const;

    // Compute cost for a specific join algorithm
    shared_ptr<PhysicalNode> computeJoinCost(
        PhysicalNodeType joinType,
        shared_ptr<PhysicalNode> left,
        shared_ptr<PhysicalNode> right,
        const Predicate& joinCond) const;

    // Estimate output rows after a predicate
    int estimateFilteredRows(const string& tableName, int inputRows,
                             const Predicate& pred) const;

    double getTT() const { return tT_; }
    double getTS() const { return tS_; }

private:
    const Catalog& catalog_;
    double tT_;     // block transfer time
    double tS_;     // seek time

    // Selectivity for a single comparison
    double comparisonSelectivity(const Predicate& pred, const string& tableName) const;

    // Buffer pages available
    int bufferPages_ = 50;
};

#endif 
