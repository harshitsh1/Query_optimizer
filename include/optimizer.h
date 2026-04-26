#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include <string>
#include <vector>
#include <memory>
#include "catalog.h"
#include "logical_plan.h"
#include "physical_plan.h"
#include "heuristic_optimizer.h"
#include "cost_estimator.h"
#include "plan_enumerator.h"

using namespace std;



struct OptimizerResult {
    string originalAST;             
    string logicalPlan;             
    string heuristicPlan;           
    string physicalPlan;            
    string unoptimizedPhysicalPlan;
    double unoptimizedCost;
    double optimizedCost;
    vector<PlanCandidate> allPlans;

    string toJSON() const;
};

class Optimizer {
public:
    explicit Optimizer(Catalog& catalog);

    OptimizerResult optimize(RelNode* ast);

    Catalog& getCatalog() { return catalog_; }

private:
    Catalog& catalog_;
};

#endif 
