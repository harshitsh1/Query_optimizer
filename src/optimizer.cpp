#include "optimizer.h"
#include <sstream>

using namespace std;


// OptimizerResult::toJSON


string OptimizerResult::toJSON() const {
    ostringstream ss;
    ss << "{";
    ss << "\"originalAST\":" << originalAST;
    ss << ",\"logicalPlan\":" << logicalPlan;
    ss << ",\"heuristicPlan\":" << heuristicPlan;
    ss << ",\"physicalPlan\":" << physicalPlan;
    ss << ",\"unoptimizedPhysicalPlan\":" << unoptimizedPhysicalPlan;
    ss << ",\"unoptimizedCost\":" << unoptimizedCost;
    ss << ",\"optimizedCost\":" << optimizedCost;
    ss << ",\"allPlans\":[";
    for (size_t i = 0; i < allPlans.size(); i++) {
        if (i > 0) ss << ",";
        ss << "{\"description\":\"" << allPlans[i].description << "\""
           << ",\"cost\":" << allPlans[i].cost;
        if (allPlans[i].plan)
            ss << ",\"plan\":" << allPlans[i].plan->toJSON();
        ss << "}";
    }
    ss << "]}";
    return ss.str();
}


// Naive Plan Builder


static shared_ptr<PhysicalNode> buildNaivePhysicalPlan(shared_ptr<LogicalNode> logical, const CostEstimator& estimator) {
    if (!logical) return nullptr;
    
    if (logical->type == LogicalNodeType::SCAN) {
        auto scan = dynamic_pointer_cast<ScanNode>(logical);
        int rows = scan->estimatedRows > 0 ? scan->estimatedRows : 1000;
        return estimator.bestScanPlan(scan->tableName, rows);
    } 
    else if (logical->type == LogicalNodeType::SELECT) {
        auto sel = dynamic_pointer_cast<SelectNode>(logical);
        auto input = buildNaivePhysicalPlan(sel->input, estimator);
        if (!input) return nullptr;
        
        auto phys = make_shared<PhysicalNode>();
        phys->type = PhysicalNodeType::FILTER;
        phys->predicate = sel->predicate;
        phys->hasPredicate = true;
        phys->children.push_back(input);
        phys->estimatedRows = estimator.estimateFilteredRows("", input->estimatedRows, sel->predicate);
        phys->estimatedBlocks = input->estimatedBlocks;
        phys->computeTotalCost(estimator.getTT(), estimator.getTS());
        return phys;
    }
    else if (logical->type == LogicalNodeType::PROJECT) {
        auto proj = dynamic_pointer_cast<ProjectNode>(logical);
        auto input = buildNaivePhysicalPlan(proj->input, estimator);
        if (!input) return nullptr;
        
        auto phys = make_shared<PhysicalNode>();
        phys->type = PhysicalNodeType::PROJECTION;
        phys->projColumns = proj->columns;
        phys->children.push_back(input);
        phys->estimatedRows = input->estimatedRows;
        phys->estimatedBlocks = input->estimatedBlocks;
        phys->computeTotalCost(estimator.getTT(), estimator.getTS());
        return phys;
    }
    else if (logical->type == LogicalNodeType::JOIN) {
        auto join = dynamic_pointer_cast<JoinNode>(logical);
        auto left = buildNaivePhysicalPlan(join->left, estimator);
        auto right = buildNaivePhysicalPlan(join->right, estimator);
        if (!left || !right) return nullptr;
        
        return estimator.computeJoinCost(PhysicalNodeType::NESTED_LOOP_JOIN, left, right, join->condition);
    }
    
    return nullptr;
}


// Optimizer


Optimizer::Optimizer(Catalog& catalog) : catalog_(catalog) {}

OptimizerResult Optimizer::optimize(RelNode* ast) {
    OptimizerResult result;

    // Step 1: Capture original AST as JSON
    // We'll reuse the parser's JSON printer via a string capture
    // For simplicity, we convert to logical plan and use its JSON
    result.originalAST = "null";

    // Step 2: Convert AST to Logical Plan
    auto logicalPlan = convertASTtoLogicalPlan(ast);
    if (!logicalPlan) {
        result.logicalPlan = "null";
        result.heuristicPlan = "null";
        result.physicalPlan = "null";
        result.unoptimizedCost = 0;
        result.optimizedCost = 0;
        return result;
    }
    result.logicalPlan = logicalPlan->toJSON();

    // Step 3: Heuristic optimization
    HeuristicOptimizer heuristic(catalog_);
    auto optimizedLogical = heuristic.optimize(logicalPlan);
    result.heuristicPlan = optimizedLogical->toJSON();

    // Step 4: Cost-based physical plan enumeration
    CostEstimator estimator(catalog_);

    // Generate unoptimized physical plan (naive 1-to-1 mapping without DP)
    auto unoptPhysical = buildNaivePhysicalPlan(logicalPlan, estimator);
    result.unoptimizedCost = unoptPhysical ? unoptPhysical->totalCost : 0;

    // Generate optimized physical plan (from heuristic-optimized logical plan)
    PlanEnumerator optEnum(catalog_, estimator);
    auto optPhysical = optEnum.findBestPlan(optimizedLogical, result.allPlans);
    result.optimizedCost = optPhysical ? optPhysical->totalCost : 0;
    
    result.unoptimizedPhysicalPlan = unoptPhysical ? unoptPhysical->toJSON() : "null";

    // Choose the better plan (lower cost) to avoid negative improvement
    if (unoptPhysical && optPhysical && unoptPhysical->totalCost < optPhysical->totalCost) {
        // Unoptimized plan is better, use it as the displayed physical plan
        result.physicalPlan = unoptPhysical->toJSON();
        result.optimizedCost = unoptPhysical->totalCost;
        // Keep allPlans so the UI can still show what the DP evaluated
    } else {
        result.physicalPlan = optPhysical ? optPhysical->toJSON() : "null";
    }

    return result;
}
