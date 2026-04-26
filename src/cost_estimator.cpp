#include "cost_estimator.h"
#include <cmath>
#include <algorithm>
#include <iostream>

using namespace std;

CostEstimator::CostEstimator(const Catalog& catalog, double tT, double tS)
    : catalog_(catalog), tT_(tT), tS_(tS) {}


double CostEstimator::comparisonSelectivity(const Predicate& pred, const string& tableName) const {
    if (pred.type != PredType::COMPARISON) return 0.5;

    string tbl = pred.left.isColumn ? pred.left.table : tableName;
    string col = pred.left.isColumn ? pred.left.column : "";

    auto colStats = catalog_.getColumn(tbl, col);
    if (!colStats || colStats->numDistinct <= 0) return 0.1;

    if (pred.op == CompOp::EQ) {
        return 1.0 / colStats->numDistinct;
    }

    if (!pred.right.isColumn && colStats->maxVal > colStats->minVal) {
        double val = pred.right.numVal;
        double range = colStats->maxVal - colStats->minVal;

        switch (pred.op) {
            case CompOp::LT:
            case CompOp::LE:
                return max(0.01, min(1.0, (val - colStats->minVal) / range));
            case CompOp::GT:
            case CompOp::GE:
                return max(0.01, min(1.0, (colStats->maxVal - val) / range));
            case CompOp::NE:
                return max(0.01, 1.0 - 1.0 / colStats->numDistinct);
            default:
                break;
        }
    }

    return 1.0 / 3.0; // default for range with no stats
}

double CostEstimator::estimateSelectivity(const Predicate& pred, const string& tableName) const {
    switch (pred.type) {
        case PredType::COMPARISON:
            return comparisonSelectivity(pred, tableName);
        case PredType::AND: {
            // Check if all children are range comparisons on the same column
            bool sameColumnRange = true;
            string tbl, col;
            double lowerBound = -1e9, upperBound = 1e9;
            
            for (const auto& child : pred.children) {
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
                    double sel = matchingRange / totalRange;
                    return max(0.01, min(1.0, sel));
                }
            }
            
            // Independence assumption 
            double sel = 1.0;
            for (const auto& child : pred.children) {
                sel *= estimateSelectivity(child, tableName);
            }
            return sel;
        }
        case PredType::OR: {
            // P(A ∨ B) = P(A) + P(B) - P(A)·P(B)
            if (pred.children.size() >= 2) {
                double s1 = estimateSelectivity(pred.children[0], tableName);
                double s2 = estimateSelectivity(pred.children[1], tableName);
                return s1 + s2 - s1 * s2;
            }
            return 0.5;
        }
        case PredType::NOT: {
            if (!pred.children.empty()) {
                return 1.0 - estimateSelectivity(pred.children[0], tableName);
            }
            return 0.5;
        }
    }
    return 0.5;
}


int CostEstimator::estimateJoinSize(const string& leftTable, int leftRows,
                                     const string& rightTable, int rightRows,
                                     const Predicate& joinCond) const {
    if (joinCond.type == PredType::COMPARISON &&
        joinCond.left.isColumn && joinCond.right.isColumn &&
        joinCond.op == CompOp::EQ) {

        string lt = joinCond.left.table;
        string lc = joinCond.left.column;
        string rt = joinCond.right.table;
        string rc = joinCond.right.column;

        auto lcol = catalog_.getColumn(lt, lc);
        auto rcol = catalog_.getColumn(rt, rc);

        if (lcol && rcol) {
            long long product = (long long)leftRows * rightRows;
            int maxDistinct = max(lcol->numDistinct, rcol->numDistinct);
            if (maxDistinct <= 0) maxDistinct = 1;
            
            return max(1, (int)(product / maxDistinct));
        }
    }

    // Cartesian product / 10
    return max(1, leftRows * rightRows / 10);
}

int CostEstimator::estimateFilteredRows(const string& tableName, int inputRows,
                                         const Predicate& pred) const {
    double sel = estimateSelectivity(pred, tableName);
    return max(1, (int)(inputRows * sel));
}


shared_ptr<PhysicalNode> CostEstimator::bestScanPlan(
    const string& tableName, int currentRows, const Predicate* pred) const
{
    auto tblStats = catalog_.getTable(tableName);
    if (!tblStats) {
        // Unknown table: default sequential scan
        auto node = make_shared<PhysicalNode>();
        node->type = PhysicalNodeType::SEQ_SCAN;
        node->tableName = tableName;
        node->blockTransfers = 100;
        node->seeks = 1;
        node->estimatedRows = currentRows;
        node->estimatedBlocks = 100;
        node->computeTotalCost(tT_, tS_);
        return node;
    }

    int br = tblStats->numBlocks;
    int nr = tblStats->numTuples;

    // ── Sequential Scan (A1): always available ──
    auto seqScan = make_shared<PhysicalNode>();
    seqScan->type = PhysicalNodeType::SEQ_SCAN;
    seqScan->tableName = tableName;
    seqScan->blockTransfers = br;
    seqScan->seeks = 1;
    seqScan->estimatedBlocks = br;
    if (pred) { 
        seqScan->predicate = *pred; 
        seqScan->hasPredicate = true; 
        double sel = estimateSelectivity(*pred, tableName);
        seqScan->estimatedRows = max(1, (int)(currentRows * sel));
    } else {
        seqScan->estimatedRows = currentRows;
    }
    seqScan->computeTotalCost(tT_, tS_);

    shared_ptr<PhysicalNode> bestPlan = seqScan;

    //  Try index scans if a predicate is given 
    if (pred && pred->type == PredType::COMPARISON && pred->left.isColumn) {
        string col = pred->left.column;
        auto indices = catalog_.findAllIndices(tableName, col);

        for (const auto* idx : indices) {
            auto idxNode = make_shared<PhysicalNode>();
            idxNode->tableName = tableName;
            idxNode->indexUsed = idx->name;
            idxNode->predicate = *pred;
            idxNode->hasPredicate = true;

            double sel = comparisonSelectivity(*pred, tableName);
            int matchingTuples = max(1, (int)(nr * sel));
            int matchingBlocks = max(1, (int)(br * sel));

            if (idx->type == IndexType::BTREE) {
                int hi = idx->height;
                idxNode->type = PhysicalNodeType::BTREE_INDEX_SCAN;

                if (pred->op == CompOp::EQ) {
                    if (idx->isClustered && idx->isUnique) {
                        // A2: Clustered, equality on key
                        idxNode->blockTransfers = hi + 1;
                        idxNode->seeks = hi + 1;
                    } else if (idx->isClustered) {
                        // A3: Clustered, equality on non-key
                        idxNode->blockTransfers = hi + matchingBlocks;
                        idxNode->seeks = hi + 1;
                    } else {
                        // A4: Secondary, equality
                        idxNode->blockTransfers = hi + matchingTuples;
                        idxNode->seeks = hi + matchingTuples;
                    }
                } else {
                    // Range query (A5/A6)
                    if (idx->isClustered) {
                        // A5: Clustered range
                        idxNode->blockTransfers = hi + matchingBlocks;
                        idxNode->seeks = hi + 1;
                    } else {
                        // A6: Secondary range
                        idxNode->blockTransfers = hi + matchingTuples;
                        idxNode->seeks = hi + matchingTuples;
                    }
                }
                idxNode->estimatedRows = matchingTuples;
                idxNode->estimatedBlocks = matchingBlocks;

            } else if (idx->type == IndexType::HASH) {
                // Hash index: equality only (14.52)
                if (pred->op != CompOp::EQ) continue;

                idxNode->type = PhysicalNodeType::HASH_INDEX_SCAN;
                int overflow = (int)idx->avgOverflowChainLen;
                idxNode->blockTransfers = 1 + overflow;
                idxNode->seeks = 1 + overflow;
                idxNode->estimatedRows = matchingTuples;
                idxNode->estimatedBlocks = 1;

            } else if (idx->type == IndexType::BITMAP) {
                // Bitmap index scan (14.71–14.75)
                if (pred->op != CompOp::EQ) continue;

                idxNode->type = PhysicalNodeType::BITMAP_INDEX_SCAN;
                int bitmapPages = max(1, nr / (8 * tblStats->blockSize) + 1);
                idxNode->blockTransfers = bitmapPages + matchingBlocks;
                idxNode->seeks = 2;
                idxNode->estimatedRows = matchingTuples;
                idxNode->estimatedBlocks = matchingBlocks;
            }

            idxNode->computeTotalCost(tT_, tS_);

            if (idxNode->totalCost < bestPlan->totalCost) {
                bestPlan = idxNode;
            }
        }
    }

    return bestPlan;
}


shared_ptr<PhysicalNode> CostEstimator::computeJoinCost(
    PhysicalNodeType joinType,
    shared_ptr<PhysicalNode> left,
    shared_ptr<PhysicalNode> right,
    const Predicate& joinCond) const
{
    auto node = make_shared<PhysicalNode>();
    node->type = joinType;
    node->joinCondition = joinCond;
    node->hasJoinCondition = true;
    node->children.push_back(left);
    node->children.push_back(right);

    int nr = left->estimatedRows;
    int ns = right->estimatedRows;
    int br = max(1, left->estimatedBlocks);
    int bs = max(1, right->estimatedBlocks);
    int bb = max(1, bufferPages_);  // buffer blocks per run

    // Estimate join output size
    string leftTbl = left->tableName;
    string rightTbl = right->tableName;
    if (leftTbl.empty() && !left->children.empty()) leftTbl = left->children[0]->tableName;
    if (rightTbl.empty() && !right->children.empty()) rightTbl = right->children[0]->tableName;

    node->estimatedRows = estimateJoinSize(leftTbl, nr, rightTbl, ns, joinCond);

    auto tblLeft = catalog_.getTable(leftTbl);
    auto tblRight = catalog_.getTable(rightTbl);
    int tupleSizeLeft = tblLeft ? tblLeft->tupleSize : 80;
    int tupleSizeRight = tblRight ? tblRight->tupleSize : 80;
    int blockSize = tblLeft ? tblLeft->blockSize : 4096;
    int outputTupleSize = tupleSizeLeft + tupleSizeRight;
    node->estimatedBlocks = max(1, (int)ceil((double)node->estimatedRows * outputTupleSize / blockSize));

    switch (joinType) {
        case PhysicalNodeType::NESTED_LOOP_JOIN:
            //n_r * b_s + b_r block transfers, n_r + b_r seeks
            node->blockTransfers = (double)nr * bs + br;
            node->seeks = nr + br;
            break;

        case PhysicalNodeType::BLOCK_NESTED_LOOP_JOIN:
            //b_r * b_s + b_r block transfers, 2 * b_r seeks
            node->blockTransfers = (double)br * bs + br;
            node->seeks = 2.0 * br;
            break;

        case PhysicalNodeType::MERGE_SORT_JOIN: {
            // b_r + b_s (+ sort cost)
            // Sort cost: 2*b * ceil(log_{M-1}(b/M)) per relation
            double sortCostR = 2.0 * br * max(1.0, ceil(log((double)br / bb) / log((double)(bb - 1))));
            double sortCostS = 2.0 * bs * max(1.0, ceil(log((double)bs / bb) / log((double)(bb - 1))));
            node->blockTransfers = sortCostR + sortCostS + br + bs;
            node->seeks = (double)(br / bb + bs / bb) * 2 + br / bb + bs / bb;
            break;
        }

        case PhysicalNodeType::HASH_JOIN:
            //3(b_r + b_s) + 4*n_h
            node->blockTransfers = 3.0 * (br + bs) + 4;
            node->seeks = 2.0 * ((double)br / bb + (double)bs / bb);
            break;

        default:
            node->blockTransfers = (double)nr * bs + br;
            node->seeks = nr + br;
            break;
    }

    node->computeTotalCost(tT_, tS_);
    return node;
}
