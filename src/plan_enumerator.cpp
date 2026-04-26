#include "plan_enumerator.h"
#include <algorithm>
#include <iostream>
#include <climits>

using namespace std;

PlanEnumerator::PlanEnumerator(const Catalog& catalog, const CostEstimator& estimator)
    : catalog_(catalog), estimator_(estimator) {}


// Extraction: pull relations and predicates from logical plan


void PlanEnumerator::extractRelationsAndPredicates(shared_ptr<LogicalNode> node) {
    relations_.clear();
    joinPredicates_.clear();
    projectionCols_.clear();
    extractFromNode(node);
}

void PlanEnumerator::extractFromNode(shared_ptr<LogicalNode> node) {
    if (!node) return;

    switch (node->type) {
        case LogicalNodeType::SCAN: {
            auto scan = dynamic_pointer_cast<ScanNode>(node);
            RelationInfo ri;
            ri.tableName = scan->tableName;
            ri.alias = scan->alias;
            ri.index = (int)relations_.size();
            relations_.push_back(ri);
            break;
        }
        case LogicalNodeType::SELECT: {
            auto sel = dynamic_pointer_cast<SelectNode>(node);
            extractFromNode(sel->input);
            classifyPredicate(sel->predicate);
            break;
        }
        case LogicalNodeType::PROJECT: {
            auto proj = dynamic_pointer_cast<ProjectNode>(node);
            projectionCols_ = proj->columns;
            extractFromNode(proj->input);
            break;
        }
        case LogicalNodeType::JOIN: {
            auto join = dynamic_pointer_cast<JoinNode>(node);
            // Record relation-index ranges for left and right subtrees
            int leftStart  = (int)relations_.size();
            extractFromNode(join->left);
            int leftEnd    = (int)relations_.size();
            extractFromNode(join->right);
            int rightEnd   = (int)relations_.size();

            // Build full subtree bitmasks (used as a fallback)
            int lm = 0, rm = 0;
            for (int i = leftStart; i < leftEnd;  i++) lm |= (1 << i);
            for (int i = leftEnd;  i < rightEnd;  i++) rm |= (1 << i);

            JoinPredInfo jp;
            jp.pred      = join->condition;
            jp.tables    = join->condition.getReferencedTables();
            jp.leftMask  = lm;
            jp.rightMask = rm;

            // Compute exact reference bits: find which specific relation (by index)
            // each operand of the join condition refers to, by searching within the
            // correct subtree range.  This disambiguates duplicate table aliases.
            auto searchRange = [&](const string& name, int startIdx, int endIdx) -> int {
                for (int i = startIdx; i < endIdx; i++) {
                    const auto& ri = relations_[i];
                    string eff = ri.alias.empty() ? ri.tableName : ri.alias;
                    if (eff == name || ri.tableName == name) return i;
                }
                return -1;
            };
            for (const auto& tbl : jp.tables) {
                int inLeft  = searchRange(tbl, leftStart, leftEnd);
                int inRight = searchRange(tbl, leftEnd,  rightEnd);
                if (inLeft  >= 0 && jp.jp_leftRefBit  < 0) jp.jp_leftRefBit  = inLeft;
                if (inRight >= 0 && jp.jp_rightRefBit < 0) jp.jp_rightRefBit = inRight;
            }

            joinPredicates_.push_back(jp);
            break;
        }
    }
}

void PlanEnumerator::classifyPredicate(const Predicate& pred) {
    // Flatten conjunctions
    if (pred.type == PredType::AND) {
        for (const auto& child : pred.children) {
            classifyPredicate(child);
        }
        return;
    }

    auto tables = pred.getReferencedTables();
    if (tables.size() <= 1) {
        // Single-table predicate: attach to the MOST RECENTLY added matching
        // relation (search newest-first) so duplicate aliases (e.g. four
        // subqueries all aliased 'b') each get their own local predicate.
        for (int i = (int)relations_.size() - 1; i >= 0; i--) {
            auto& ri = relations_[i];
            string eff = ri.alias.empty() ? ri.tableName : ri.alias;
            if (tables.empty() || tables.count(eff) || tables.count(ri.tableName)) {
                ri.localPredicates.push_back(pred);
                return;
            }
        }
    }

    // Multi-table predicate: recorded as join predicate (masks filled by caller)
    JoinPredInfo jp;
    jp.pred   = pred;
    jp.tables = tables;
    // leftMask / rightMask stay 0 — findJoinPredicates will fall back to
    // string-based lookup for these (e.g. outer WHERE pushed down after join).
    joinPredicates_.push_back(jp);
}


// Utility


string PlanEnumerator::getTableName(int idx) const {
    if (idx >= 0 && idx < (int)relations_.size())
        return relations_[idx].tableName;
    return "";
}

string PlanEnumerator::getEffectiveName(int idx) const {
    if (idx >= 0 && idx < (int)relations_.size()) {
        return relations_[idx].alias.empty() ? relations_[idx].tableName : relations_[idx].alias;
    }
    return "";
}

int PlanEnumerator::relationBit(const string& name) const {
    for (const auto& ri : relations_) {
        string eff = ri.alias.empty() ? ri.tableName : ri.alias;
        if (eff == name || ri.tableName == name)
            return ri.index;
    }
    return -1;
}

vector<Predicate> PlanEnumerator::findJoinPredicates(int leftMask, int rightMask) {
    vector<Predicate> result;
    for (const auto& jp : joinPredicates_) {
        bool connects = false;

        if (jp.jp_leftRefBit >= 0 && jp.jp_rightRefBit >= 0) {
            // Best case: we know the EXACT relation indices referenced on each side.
            // Forward:  left operand in query's left set, right operand in query's right set.
            // Reverse:  left operand in query's right set, right operand in query's left set.
            bool fwd = (leftMask  & (1 << jp.jp_leftRefBit))  &&
                       (rightMask & (1 << jp.jp_rightRefBit));
            bool rev = (rightMask & (1 << jp.jp_leftRefBit))  &&
                       (leftMask  & (1 << jp.jp_rightRefBit));
            connects = fwd || rev;
        } else {
            // Fallback: string-based table-name lookup (for predicates without exact bits,
            // e.g. multi-table predicates pushed down from outer WHERE clauses).
            bool leftOk = false, rightOk = false;
            for (const auto& tbl : jp.tables) {
                int bit = relationBit(tbl);
                if (bit >= 0) {
                    if (leftMask  & (1 << bit)) leftOk  = true;
                    if (rightMask & (1 << bit)) rightOk = true;
                }
            }
            connects = leftOk && rightOk;
        }

        if (connects) result.push_back(jp.pred);
    }
    return result;
}


// Single-relation access plan


PlanEnumerator::DPEntry PlanEnumerator::singleRelationPlan(int relIdx) {
    const auto& ri = relations_[relIdx];
    auto tblStats = catalog_.getTable(ri.tableName);
    int baseRows = tblStats ? tblStats->numTuples : 1000;

    shared_ptr<PhysicalNode> bestPlan;

    // Find a predicate that can potentially be used for an index scan
    const Predicate* bestPred = nullptr;
    for (const auto& p : ri.localPredicates) {
        if (p.type == PredType::COMPARISON && p.left.isColumn) {
            bestPred = &p;
            break;
        }
    }

    // Start with a scan for the relation, utilizing index if bestPred is applicable
    bestPlan = estimator_.bestScanPlan(ri.tableName, baseRows, bestPred);

    // Group predicates by column to detect range predicates on the same column
    map<string, vector<Predicate>> predicatesByColumn;
    for (const auto& pred : ri.localPredicates) {
        if (pred.type == PredType::COMPARISON && pred.left.isColumn) {
            string key = pred.left.table + "." + pred.left.column;
            predicatesByColumn[key].push_back(pred);
        }
    }

    // Apply remaining local predicates as simple filters
    // For columns with multiple range predicates, combine them into an AND predicate
    for (const auto& kv167 : predicatesByColumn) {
        const string& colKey = kv167.first;
        const vector<Predicate>& preds = kv167.second;
        if (preds.size() > 1) {
            // Check if all are range predicates (GT/GE/LT/LE)
            bool allRange = true;
            for (const auto& p : preds) {
                if (p.op != CompOp::GT && p.op != CompOp::GE && 
                    p.op != CompOp::LT && p.op != CompOp::LE) {
                    allRange = false;
                    break;
                }
            }
            
            if (allRange) {
                // Combine into a single AND predicate
                Predicate combined;
                combined.type = PredType::AND;
                combined.children = preds;
                
                // Always apply the combined filter for range predicates on same column
                // even if one was used in the scan, to ensure correct row estimation
                auto filter = make_shared<PhysicalNode>();
                filter->type = PhysicalNodeType::FILTER;
                filter->predicate = combined;
                filter->hasPredicate = true;
                // Use base rows for combined selectivity calculation
                filter->estimatedRows = estimator_.estimateFilteredRows(
                    ri.tableName, baseRows, combined);
                filter->estimatedBlocks = bestPlan->estimatedBlocks;
                filter->children.push_back(bestPlan);
                filter->computeTotalCost(estimator_.getTT(), estimator_.getTS());
                bestPlan = filter;
                continue; // Skip individual application of these predicates
            }
        }
        
        // Apply predicates individually (either single predicate or non-range group)
        for (const auto& pred : preds) {
            // If bestPlan already used this exact predicate (either pushed into IndexScan or SeqScan), skip redundant filter
            if (bestPlan->hasPredicate && bestPlan->predicate.left.column == pred.left.column && bestPlan->predicate.op == pred.op) {
                continue;
            }

            auto filter = make_shared<PhysicalNode>();
            filter->type = PhysicalNodeType::FILTER;
            filter->predicate = pred;
            filter->hasPredicate = true;
            filter->estimatedRows = estimator_.estimateFilteredRows(
                ri.tableName, bestPlan->estimatedRows, pred);
            filter->estimatedBlocks = bestPlan->estimatedBlocks;
            filter->children.push_back(bestPlan);
            filter->computeTotalCost(estimator_.getTT(), estimator_.getTS());
            bestPlan = filter;
        }
    }

    DPEntry entry;
    entry.plan = bestPlan;
    entry.cost = bestPlan->totalCost;
    entry.estimatedRows = bestPlan->estimatedRows;
    return entry;
}


// System R DP — findBest (§16.29–16.31)
// Left-deep join trees


PlanEnumerator::DPEntry PlanEnumerator::findBest(int subset) {
    // Check memoization
    auto it = dpTable_.find(subset);
    if (it != dpTable_.end()) return it->second;

    int n = (int)relations_.size();

    // Count bits (number of relations in subset)
    int bitCount = 0;
    for (int i = 0; i < n; i++) {
        if (subset & (1 << i)) bitCount++;
    }

    // Base case: single relation
    if (bitCount == 1) {
        for (int i = 0; i < n; i++) {
            if (subset == (1 << i)) {
                auto entry = singleRelationPlan(i);
                dpTable_[subset] = entry;
                return entry;
            }
        }
    }

    DPEntry bestEntry;
    bestEntry.cost = 1e18;

    // Left-deep: try removing each single relation as the right input
    for (int i = 0; i < n; i++) {
        int rightBit = 1 << i;
        if (!(subset & rightBit)) continue;

        int leftSubset = subset & ~rightBit;
        if (leftSubset == 0) continue;

        DPEntry leftEntry = findBest(leftSubset);
        DPEntry rightEntry = singleRelationPlan(i);

        // Find applicable join predicates
        auto joinPreds = findJoinPredicates(leftSubset, rightBit);

        // Build join predicate (conjunction of all applicable)
        Predicate joinPred;
        if (joinPreds.empty()) {
            // Cross product (no join condition found) — very expensive
            joinPred.type = PredType::COMPARISON;
            joinPred.op = CompOp::EQ;
            // Dummy condition → will result in NL join with full product
        } else if (joinPreds.size() == 1) {
            joinPred = joinPreds[0];
        } else {
            joinPred.type = PredType::AND;
            joinPred.children = joinPreds;
        }

        // Only use simple Nested Loop Join for simplicity
        vector<PhysicalNodeType> joinAlgos = {
            PhysicalNodeType::NESTED_LOOP_JOIN
        };

        for (auto algo : joinAlgos) {
            auto joinPlan = estimator_.computeJoinCost(
                algo, leftEntry.plan, rightEntry.plan, joinPred);

            double totalCost = joinPlan->totalCost;

            if (totalCost < bestEntry.cost) {
                bestEntry.plan = joinPlan;
                bestEntry.cost = totalCost;
                bestEntry.estimatedRows = joinPlan->estimatedRows;
            }
        }
    }

    dpTable_[subset] = bestEntry;
    return bestEntry;
}


// Public: find best plan


shared_ptr<PhysicalNode> PlanEnumerator::findBestPlan(
    shared_ptr<LogicalNode> logicalPlan,
    vector<PlanCandidate>& allPlans)
{
    // Extract relations, predicates, projections
    extractRelationsAndPredicates(logicalPlan);

    if (relations_.empty()) {
        return nullptr;
    }

    // Clear DP table
    dpTable_.clear();

    int n = (int)relations_.size();
    int fullSet = (1 << n) - 1;

    // Run DP
    DPEntry best = findBest(fullSet);

    // Collect all enumerated plans for UI display
    for (auto& kv337 : dpTable_) {
        int mask = kv337.first;
        DPEntry& entry = kv337.second;
        int bits = 0;
        for (int i = 0; i < n; i++) if (mask & (1 << i)) bits++;
        if (bits >= 2) { // Only show multi-relation plans
            string desc;
            for (int i = 0; i < n; i++) {
                if (mask & (1 << i)) {
                    if (!desc.empty()) desc += " ⨝ ";
                    desc += getEffectiveName(i);
                }
            }
            allPlans.push_back({desc, entry.cost, entry.plan});
        }
    }

    // Sort by cost
    sort(allPlans.begin(), allPlans.end(),
         [](const PlanCandidate& a, const PlanCandidate& b) { return a.cost < b.cost; });

    // Wrap with projection if needed
    if (!projectionCols_.empty() && best.plan) {
        auto proj = make_shared<PhysicalNode>();
        proj->type = PhysicalNodeType::PROJECTION;
        proj->projColumns = projectionCols_;
        proj->estimatedRows = best.estimatedRows;
        proj->estimatedBlocks = best.plan->estimatedBlocks;
        proj->children.push_back(best.plan);
        proj->computeTotalCost(estimator_.getTT(), estimator_.getTS());
        return proj;
    }

    return best.plan;
}