#ifndef PHYSICAL_PLAN_H
#define PHYSICAL_PLAN_H

#include <string>
#include <vector>
#include <memory>
#include "logical_plan.h"
#include "catalog.h"

using namespace std;


enum class PhysicalNodeType {
    SEQ_SCAN,
    BTREE_INDEX_SCAN,
    HASH_INDEX_SCAN,
    BITMAP_INDEX_SCAN,
    FILTER,
    NESTED_LOOP_JOIN,
    BLOCK_NESTED_LOOP_JOIN,
    MERGE_SORT_JOIN,
    HASH_JOIN,
    PROJECTION
};

string physicalNodeTypeName(PhysicalNodeType t);


class PhysicalNode {
public:
    PhysicalNodeType type;

    // Cost components 
    double blockTransfers = 0;      // b * t_T
    double seeks = 0;               // S * t_S
    double totalCost = 0;           // blockTransfers * tT + seeks * tS

    int estimatedRows = 0;
    int estimatedBlocks = 0;
    string indexUsed;               // name of index if applicable
    string tableName;               // for scan nodes

    // Children
    vector<shared_ptr<PhysicalNode>> children;

    // Predicate 
    Predicate predicate;
    bool hasPredicate = false;

    // For projection nodes
    vector<ColumnRef> projColumns;

    // Join condition
    Predicate joinCondition;
    bool hasJoinCondition = false;

    virtual ~PhysicalNode() = default;

    // Compute totalCost from block transfers and seeks
    void computeTotalCost(double tT = 0.1, double tS = 4.0);

    string toJSON() const;
    string toString(int indent = 0) const;
};

#endif
