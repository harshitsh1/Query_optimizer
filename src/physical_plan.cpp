#include "physical_plan.h"

using namespace std;

string physicalNodeTypeName(PhysicalNodeType t) {
    switch (t) {
        case PhysicalNodeType::SEQ_SCAN:              return "SeqScan";
        case PhysicalNodeType::BTREE_INDEX_SCAN:      return "B+TreeIndexScan";
        case PhysicalNodeType::HASH_INDEX_SCAN:       return "HashIndexScan";
        case PhysicalNodeType::BITMAP_INDEX_SCAN:     return "BitmapIndexScan";
        case PhysicalNodeType::FILTER:                return "Filter";
        case PhysicalNodeType::NESTED_LOOP_JOIN:      return "NestedLoopJoin";
        case PhysicalNodeType::BLOCK_NESTED_LOOP_JOIN:return "BlockNestedLoopJoin";
        case PhysicalNodeType::MERGE_SORT_JOIN:       return "MergeSortJoin";
        case PhysicalNodeType::HASH_JOIN:             return "HashJoin";
        case PhysicalNodeType::PROJECTION:            return "Projection";
    }
    return "Unknown";
}

void PhysicalNode::computeTotalCost(double tT, double tS) {
    totalCost = blockTransfers * tT + seeks * tS;
    // Add children costs
    for (auto& child : children) {
        totalCost += child->totalCost;
    }
}

string PhysicalNode::toJSON() const {
    ostringstream ss;
    ss << "{\"type\":\"" << physicalNodeTypeName(type) << "\"";
    if (!tableName.empty())
        ss << ",\"table\":\"" << tableName << "\"";
    if (!indexUsed.empty())
        ss << ",\"index\":\"" << indexUsed << "\"";

    ss << ",\"blockTransfers\":" << blockTransfers
       << ",\"seeks\":" << seeks
       << ",\"totalCost\":" << totalCost
       << ",\"estimatedRows\":" << estimatedRows
       << ",\"estimatedBlocks\":" << estimatedBlocks;

    if (hasPredicate)
        ss << ",\"predicate\":" << predicate.toJSON();
    if (hasJoinCondition)
        ss << ",\"joinCondition\":" << joinCondition.toJSON();

    if (!projColumns.empty()) {
        ss << ",\"columns\":[";
        for (size_t i = 0; i < projColumns.size(); i++) {
            if (i > 0) ss << ",";
            ss << projColumns[i].toJSON();
        }
        ss << "]";
    }

    if (!children.empty()) {
        ss << ",\"children\":[";
        for (size_t i = 0; i < children.size(); i++) {
            if (i > 0) ss << ",";
            ss << children[i]->toJSON();
        }
        ss << "]";
    }

    ss << "}";
    return ss.str();
}

string PhysicalNode::toString(int indent) const {
    string pad(indent * 2, ' ');
    ostringstream ss;
    ss << pad << physicalNodeTypeName(type);
    if (!tableName.empty()) ss << "(" << tableName << ")";
    if (!indexUsed.empty()) ss << " [index=" << indexUsed << "]";
    ss << " cost=" << totalCost << " rows=" << estimatedRows;
    if (hasPredicate)      ss << " pred=(" << predicate.toString() << ")";
    if (hasJoinCondition)  ss << " on=(" << joinCondition.toString() << ")";
    ss << "\n";
    for (auto& child : children) {
        ss << child->toString(indent + 1);
    }
    return ss.str();
}
