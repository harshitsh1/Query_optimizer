#ifndef CATALOG_H
#define CATALOG_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <sstream>

using namespace std;

enum class IndexType { BTREE, HASH, BITMAP };

struct IndexInfo {
    string name;
    IndexType type;
    string tableName;
    vector<string> columns;
    bool isClustered;
    bool isUnique;

    int fanout = 100;
    int height = 0;

    int numBuckets = 0;
    double avgOverflowChainLen = 0.0;

    void computeHeight(int numDistinct) {
        if (type == IndexType::BTREE && fanout > 2 && numDistinct > 0) {
            int half = fanout / 2;
            height = (int)ceil(log((double)numDistinct) / log((double)half));
            if (height < 1) height = 1;
        }
    }

    string typeString() const {
        switch (type) {
            case IndexType::BTREE:  return "B+Tree";
            case IndexType::HASH:   return "Hash";
            case IndexType::BITMAP: return "Bitmap";
        }
        return "Unknown";
    }
};

struct ColumnStats {
    string name;
    string dataType;
    int numDistinct;
    double minVal, maxVal;
    bool isPrimaryKey = false;
    bool isForeignKey = false;
    string fkRefTable;
    string fkRefColumn;
};

struct TableStats {
    string name;
    int numTuples;
    int tupleSize;
    int blockSize = 4096;
    int numBlocks;
    int blockingFactor;
    vector<ColumnStats> columns;
    vector<IndexInfo> indices;

    void computeDerived() {
        if (tupleSize > 0) {
            numBlocks = (int)ceil(((double)numTuples * tupleSize) / blockSize);
            blockingFactor = blockSize / tupleSize;
            if (blockingFactor < 1) blockingFactor = 1;
        }
        for (auto& idx : indices) {
            if (idx.type == IndexType::BTREE && !idx.columns.empty()) {
                for (const auto& col : columns) {
                    if (col.name == idx.columns[0]) {
                        idx.computeHeight(col.numDistinct);
                        break;
                    }
                }
            }
        }
    }

    const ColumnStats* getColumn(const string& colName) const {
        for (const auto& c : columns) {
            if (c.name == colName) return &c;
        }
        return nullptr;
    }
};

class Catalog {
public:
    const TableStats* getTable(const string& name) const;
    const ColumnStats* getColumn(const string& table, const string& col) const;
    const IndexInfo* findIndex(const string& table, const string& col) const;
    vector<const IndexInfo*> findAllIndices(const string& table, const string& col) const;

    string toJSON() const;

    const unordered_map<string, TableStats>& getAllTables() const { return tables_; }

    bool loadFromPostgres(const string& conninfo);
    
    void initUniversityDB();

private:
    unordered_map<string, TableStats> tables_;
    void addTable(const TableStats& t);
};

#endif 
