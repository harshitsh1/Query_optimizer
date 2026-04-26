#include "heuristic_optimizer.h"
#include <algorithm>
#include <iostream>

using namespace std;

HeuristicOptimizer::HeuristicOptimizer(const Catalog& catalog) : catalog_(catalog) {}


// Main entry point


shared_ptr<LogicalNode> HeuristicOptimizer::optimize(shared_ptr<LogicalNode> plan) {
    auto result = plan->clone();

    // Step 1: Push selections down 
    result = pushdownSelections(result);

    // Step 2: Reorder joins
    result = reorderJoins(result);

    // Step 3: Push projections down
    result = pushdownProjections(result);

    // Step 4: Annotate estimated row counts
    estimateRows(result);

    return result;
}


// Selection Pushdown 


void HeuristicOptimizer::flattenConjunction(const Predicate& pred, vector<Predicate>& out) {
    if (pred.type == PredType::AND) {
        for (const auto& child : pred.children) {
            flattenConjunction(child, out);
        }
    } else {
        out.push_back(pred);
    }
}

Predicate HeuristicOptimizer::buildConjunction(const vector<Predicate>& preds) {
    if (preds.empty()) {
        Predicate p;
        p.type = PredType::COMPARISON;
        return p;
    }
    if (preds.size() == 1) return preds[0];

    Predicate result = preds[0];
    for (size_t i = 1; i < preds.size(); i++) {
        Predicate conj;
        conj.type = PredType::AND;
        conj.children.push_back(result);
        conj.children.push_back(preds[i]);
        result = conj;
    }
    return result;
}

shared_ptr<LogicalNode> HeuristicOptimizer::pushPredicate(
    shared_ptr<LogicalNode> node, const Predicate& pred)
{
    auto refTables = pred.getReferencedTables();

    if (node->type == LogicalNodeType::SCAN) {
        auto scan = dynamic_pointer_cast<ScanNode>(node);
        string eff = scan->effectiveName();
        // Predicate references only this table → push it here
        if (refTables.size() <= 1 &&
            (refTables.empty() || refTables.count(eff) || refTables.count(scan->tableName))) {
            return make_shared<SelectNode>(node, pred);
        }
        // Can't push further
        return make_shared<SelectNode>(node, pred);
    }

    if (node->type == LogicalNodeType::JOIN) {
        auto join = dynamic_pointer_cast<JoinNode>(node);
        auto leftTables  = join->left->getOutputTables();
        auto rightTables = join->right->getOutputTables();

        // Check if predicate references only left-side or only right-side tables
        bool allLeft = true, allRight = true;
        for (const auto& t : refTables) {
            if (!leftTables.count(t))  allLeft  = false;
            if (!rightTables.count(t)) allRight = false;
        }

        if (allLeft) {
            // push into left subtree
            join->left = pushPredicate(join->left, pred);
            return join;
        }
        if (allRight) {
            //  push into right subtree
            join->right = pushPredicate(join->right, pred);
            return join;
        }

        // The predicate references both sides — it's a join condition.
        // Rule: fold it INTO the JOIN condition instead of leaving a SELECT above.
        // This is critical for comma-syntax queries (FROM A, B WHERE A.id = B.id),
        // where the parser emits a dummy join condition and puts the equijoin in WHERE.
        auto& existingCond = join->condition;
        bool isDummy = (existingCond.type == PredType::COMPARISON &&
                        existingCond.left.table.empty() &&
                        existingCond.left.column.empty());
        if (isDummy) {
            // Replace the placeholder with the real join predicate
            join->condition = pred;
        } else {
            // AND the new predicate with the existing real condition
            Predicate combined;
            combined.type = PredType::AND;
            combined.children.push_back(existingCond);
            combined.children.push_back(pred);
            join->condition = combined;
        }
        return join;
    }


    if (node->type == LogicalNodeType::SELECT) {
        auto sel = dynamic_pointer_cast<SelectNode>(node);
        sel->input = pushPredicate(sel->input, pred);
        return sel;
    }

    if (node->type == LogicalNodeType::PROJECT) {
        auto proj = dynamic_pointer_cast<ProjectNode>(node);
        proj->input = pushPredicate(proj->input, pred);
        return proj;
    }

    return make_shared<SelectNode>(node, pred);
}

shared_ptr<LogicalNode> HeuristicOptimizer::pushdownSelections(shared_ptr<LogicalNode> node) {
    if (!node) return nullptr;

    if (node->type == LogicalNodeType::SELECT) {
        auto sel = dynamic_pointer_cast<SelectNode>(node);
        // First, recursively optimize the input
        sel->input = pushdownSelections(sel->input);

        // Flatten conjunctive predicates
        vector<Predicate> atomicPreds;
        flattenConjunction(sel->predicate, atomicPreds);

        // Push each predicate as deep as possible
        shared_ptr<LogicalNode> result = sel->input;
        for (const auto& pred : atomicPreds) {
            result = pushPredicate(result, pred);
        }
        return result;
    }

    if (node->type == LogicalNodeType::PROJECT) {
        auto proj = dynamic_pointer_cast<ProjectNode>(node);
        proj->input = pushdownSelections(proj->input);
        return proj;
    }

    if (node->type == LogicalNodeType::JOIN) {
        auto join = dynamic_pointer_cast<JoinNode>(node);
        join->left = pushdownSelections(join->left);
        join->right = pushdownSelections(join->right);
        return join;
    }

    return node;
}


// Join Reordering


shared_ptr<LogicalNode> HeuristicOptimizer::reorderJoins(shared_ptr<LogicalNode> node) {
    if (!node) return nullptr;

    if (node->type == LogicalNodeType::JOIN) {
        auto join = dynamic_pointer_cast<JoinNode>(node);
        join->left = reorderJoins(join->left);
        join->right = reorderJoins(join->right);

        // Heuristic: put smaller relation on the left (outer)
        int leftRows = estimateRows(join->left);
        int rightRows = estimateRows(join->right);
        if (rightRows < leftRows) {
            swap(join->left, join->right);
            // Swap predicate sides too if it's a comparison
            // the optimizer handles this correctly regardless
        }
        return join;
    }

    if (node->type == LogicalNodeType::PROJECT) {
        auto proj = dynamic_pointer_cast<ProjectNode>(node);
        proj->input = reorderJoins(proj->input);
        return proj;
    }

    if (node->type == LogicalNodeType::SELECT) {
        auto sel = dynamic_pointer_cast<SelectNode>(node);
        sel->input = reorderJoins(sel->input);
        return sel;
    }

    return node;
}


// Projection Pushdown 


shared_ptr<LogicalNode> HeuristicOptimizer::pushdownProjections(shared_ptr<LogicalNode> node) {
    // For now, keep projections at top level
    // Full pushdown requires tracking needed columns through joins,
    //  which adds complexity; heuristic still correct without it
    return node;
}


// Row Estimation (simple heuristic using catalog)


int HeuristicOptimizer::estimateRows(shared_ptr<LogicalNode> node) {
    if (!node) return 0;

    if (node->type == LogicalNodeType::SCAN) {
        auto scan = dynamic_pointer_cast<ScanNode>(node);
        auto tbl = catalog_.getTable(scan->tableName);
        int rows = tbl ? tbl->numTuples : 1000;
        node->estimatedRows = rows;
        return rows;
    }

    if (node->type == LogicalNodeType::SELECT) {
        auto sel = dynamic_pointer_cast<SelectNode>(node);
        int inputRows = estimateRows(sel->input);
        // Default selectivity: 1/10 for simple predicates
        double selectivity = 0.1;

        // Check if input is also a SELECT on the same table with a range predicate
        // If so, calculate combined selectivity instead of applying sequentially
        if (sel->input->type == LogicalNodeType::SELECT && 
            sel->predicate.type == PredType::COMPARISON && sel->predicate.left.isColumn) {
            auto innerSel = dynamic_pointer_cast<SelectNode>(sel->input);
            if (innerSel->predicate.type == PredType::COMPARISON && innerSel->predicate.left.isColumn) {
                auto outerTables = sel->predicate.getReferencedTables();
                auto innerTables = innerSel->predicate.getReferencedTables();
                
                // Check if both predicates reference the same table and column
                bool sameColumn = false;
                if (!outerTables.empty() && !innerTables.empty() &&
                    sel->predicate.left.table == innerSel->predicate.left.table &&
                    sel->predicate.left.column == innerSel->predicate.left.column) {
                    sameColumn = true;
                }
                
                // Check if both are range predicates (GT/GE/LT/LE)
                bool bothRange = (sel->predicate.op == CompOp::GT || sel->predicate.op == CompOp::GE ||
                                  sel->predicate.op == CompOp::LT || sel->predicate.op == CompOp::LE) &&
                                 (innerSel->predicate.op == CompOp::GT || innerSel->predicate.op == CompOp::GE ||
                                  innerSel->predicate.op == CompOp::LT || innerSel->predicate.op == CompOp::LE);
                
                if (sameColumn && bothRange) {
                    string tbl = sel->predicate.left.table;
                    string col = sel->predicate.left.column;
                    auto colStats = catalog_.getColumn(tbl, col);
                    
                    if (colStats && colStats->maxVal > colStats->minVal && 
                        !sel->predicate.right.isColumn && !innerSel->predicate.right.isColumn) {
                        // Calculate combined range
                        double lowerBound = colStats->minVal;
                        double upperBound = colStats->maxVal;
                        
                        // Extract bounds from both predicates
                        auto extractBound = [&](const Predicate& p, bool isLower) {
                            double val = p.right.numVal;
                            if (p.op == CompOp::GT || p.op == CompOp::GE) {
                                if (isLower) lowerBound = max(lowerBound, val);
                            } else if (p.op == CompOp::LT || p.op == CompOp::LE) {
                                if (!isLower) upperBound = min(upperBound, val);
                            }
                        };
                        
                        extractBound(sel->predicate, sel->predicate.op == CompOp::GT || sel->predicate.op == CompOp::GE);
                        extractBound(innerSel->predicate, innerSel->predicate.op == CompOp::GT || innerSel->predicate.op == CompOp::GE);
                        
                        // Calculate combined selectivity
                        double totalRange = colStats->maxVal - colStats->minVal;
                        double matchingRange = max(0.0, upperBound - lowerBound);
                        selectivity = matchingRange / totalRange;
                        selectivity = max(0.01, min(1.0, selectivity));
                        
                        // Use the base rows (scan rows) instead of input rows
                        // since we're calculating combined selectivity
                        if (innerSel->input->type == LogicalNodeType::SCAN) {
                            auto scan = dynamic_pointer_cast<ScanNode>(innerSel->input);
                            auto tblInfo = catalog_.getTable(scan->tableName);
                            inputRows = tblInfo ? tblInfo->numTuples : 1000;
                        }
                        
                        int rows = max(1, (int)(inputRows * selectivity));
                        node->estimatedRows = rows;
                        return rows;
                    }
                }
            }
        }

        // Try to get better estimate from catalog
        if (sel->predicate.type == PredType::COMPARISON && sel->predicate.left.isColumn) {
            string tbl = sel->predicate.left.table;
            string col = sel->predicate.left.column;
            auto colStats = catalog_.getColumn(tbl, col);
            if (colStats && colStats->numDistinct > 0) {
                if (sel->predicate.op == CompOp::EQ) {
                    selectivity = 1.0 / colStats->numDistinct;
                } else {
                    // Range: assume 1/3 selectivity or compute from min/max
                    if (!sel->predicate.right.isColumn && colStats->maxVal > colStats->minVal) {
                        double val = sel->predicate.right.numVal;
                        if (sel->predicate.op == CompOp::LT || sel->predicate.op == CompOp::LE) {
                            selectivity = (val - colStats->minVal) / (colStats->maxVal - colStats->minVal);
                        } else if (sel->predicate.op == CompOp::GT || sel->predicate.op == CompOp::GE) {
                            selectivity = (colStats->maxVal - val) / (colStats->maxVal - colStats->minVal);
                        } else {
                            selectivity = 1.0 / 3.0;
                        }
                        selectivity = max(0.01, min(1.0, selectivity));
                    } else {
                        selectivity = 1.0 / 3.0;
                    }
                }
            }
        } else if (sel->predicate.type == PredType::AND) {
            // Handle AND predicates - check if they are range comparisons on the same column
            bool sameColumnRange = true;
            string tbl, col;
            double lowerBound = -1e9, upperBound = 1e9;
            
            for (const auto& child : sel->predicate.children) {
                if (child.type != PredType::COMPARISON || !child.left.isColumn || child.right.isColumn) {
                    sameColumnRange = false;
                    break;
                }
                
                if (tbl.empty()) {
                    tbl = child.left.table;
                    col = child.left.column;
                } else if (child.left.table != tbl || child.left.column != col) {
                    sameColumnRange = false;
                    break;
                }
                
                double val = child.right.numVal;
                if (child.op == CompOp::GT || child.op == CompOp::GE) {
                    lowerBound = max(lowerBound, val);
                } else if (child.op == CompOp::LT || child.op == CompOp::LE) {
                    upperBound = min(upperBound, val);
                } else {
                    sameColumnRange = false;
                    break;
                }
            }
            
            if (sameColumnRange && !tbl.empty() && !col.empty()) {
                auto colStats = catalog_.getColumn(tbl, col);
                if (colStats && colStats->maxVal > colStats->minVal) {
                    // Combined range selectivity
                    double totalRange = colStats->maxVal - colStats->minVal;
                    double matchingRange = max(0.0, upperBound - lowerBound);
                    selectivity = matchingRange / totalRange;
                    selectivity = max(0.01, min(1.0, selectivity));
                } else {
                    selectivity = 0.1; // Default for AND
                }
            } else {
                // Default: multiply selectivities of children
                selectivity = 0.1;
                for (size_t i = 0; i < sel->predicate.children.size(); i++) {
                    selectivity *= 0.5; // Conservative estimate
                }
            }
        }

        int rows = max(1, (int)(inputRows * selectivity));
        node->estimatedRows = rows;
        return rows;
    }

    if (node->type == LogicalNodeType::PROJECT) {
        auto proj = dynamic_pointer_cast<ProjectNode>(node);
        int rows = estimateRows(proj->input);
        node->estimatedRows = rows;
        return rows;
    }

    if (node->type == LogicalNodeType::JOIN) {
        auto join = dynamic_pointer_cast<JoinNode>(node);
        int leftRows = estimateRows(join->left);
        int rightRows = estimateRows(join->right);

        // Join size estimation (§16.46-16.48)
        int joinSize = leftRows * rightRows / 10; // default

        if (join->condition.type == PredType::COMPARISON &&
            join->condition.left.isColumn && join->condition.right.isColumn) {
            string lt = join->condition.left.table;
            string lc = join->condition.left.column;
            string rt = join->condition.right.table;
            string rc = join->condition.right.column;

            auto lcol = catalog_.getColumn(lt, lc);
            auto rcol = catalog_.getColumn(rt, rc);

            if (lcol && rcol) {
                // Textbook join cardinality formula: (N_R * N_S) / max(V(A, R), V(A, S))
                long long product = (long long)leftRows * rightRows;
                int maxDistinct = max(lcol->numDistinct, rcol->numDistinct);
                if (maxDistinct <= 0) maxDistinct = 1;
                
                joinSize = max(1, (int)(product / maxDistinct));
            }
        }

        joinSize = max(1, joinSize);
        node->estimatedRows = joinSize;
        return joinSize;
    }

    return 1000;
}
