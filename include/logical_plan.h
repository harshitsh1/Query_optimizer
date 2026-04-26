#ifndef LOGICAL_PLAN_H
#define LOGICAL_PLAN_H

#include <string>
#include <vector>
#include <memory>
#include <set>
#include <map>
#include <sstream>

using namespace std;

extern "C" {
    #include "sql_parser.h"
}


enum class PredType { COMPARISON, AND, OR, NOT };
enum class CompOp   { EQ, LT, GT, LE, GE, NE };

struct Operand {
    bool isColumn = false;
    string table;
    string column;
    string strVal;
    double numVal = 0;
    int literalType = 0;
};

struct Predicate {
    PredType type = PredType::COMPARISON;
    CompOp op = CompOp::EQ;
    Operand left;
    Operand right;
    vector<Predicate> children;

    set<string> getReferencedTables() const;
    string toJSON() const;
    string toString() const;
    string opString() const;
};

struct ColumnRef {
    string table;
    string column;

    string toJSON() const {
        return "{\"table\":\"" + table + "\",\"column\":\"" + column + "\"}";
    }
};


enum class LogicalNodeType { SCAN, SELECT, PROJECT, JOIN };

class LogicalNode {
public:
    LogicalNodeType type;
    int estimatedRows = 0;

    virtual ~LogicalNode() = default;
    virtual string toJSON() const = 0;
    virtual string toString(int indent = 0) const = 0;
    virtual set<string> getOutputTables() const = 0;
    virtual shared_ptr<LogicalNode> clone() const = 0;
};

class ScanNode : public LogicalNode {
public:
    string tableName;
    string alias;

    ScanNode(const string& table, const string& a = "");
    string toJSON() const override;
    string toString(int indent = 0) const override;
    set<string> getOutputTables() const override;
    shared_ptr<LogicalNode> clone() const override;

    string effectiveName() const { return alias.empty() ? tableName : alias; }
};

class SelectNode : public LogicalNode {
public:
    shared_ptr<LogicalNode> input;
    Predicate predicate;

    SelectNode(shared_ptr<LogicalNode> in, const Predicate& pred);
    string toJSON() const override;
    string toString(int indent = 0) const override;
    set<string> getOutputTables() const override;
    shared_ptr<LogicalNode> clone() const override;
};

class ProjectNode : public LogicalNode {
public:
    shared_ptr<LogicalNode> input;
    vector<ColumnRef> columns;

    ProjectNode(shared_ptr<LogicalNode> in, const vector<ColumnRef>& cols);
    string toJSON() const override;
    string toString(int indent = 0) const override;
    set<string> getOutputTables() const override;
    shared_ptr<LogicalNode> clone() const override;
};

class JoinNode : public LogicalNode {
public:
    shared_ptr<LogicalNode> left;
    shared_ptr<LogicalNode> right;
    Predicate condition;

    JoinNode(shared_ptr<LogicalNode> l, shared_ptr<LogicalNode> r, const Predicate& cond);
    string toJSON() const override;
    string toString(int indent = 0) const override;
    set<string> getOutputTables() const override;
    shared_ptr<LogicalNode> clone() const override;
};


struct AliasMapping {
    string alias;
    string baseTable;
    map<string, string> columnMapping; 
};

shared_ptr<LogicalNode> convertASTtoLogicalPlan(RelNode* ast, const vector<AliasMapping>& parentAliasMap = {});
Predicate convertCondition(Condition* cond);

// Alias resolution functions
void resolvePredicateAliases(Predicate& pred, const vector<AliasMapping>& aliasMap);
void resolveColumnRefAliases(ColumnRef& col, const vector<AliasMapping>& aliasMap);
vector<AliasMapping> buildAliasMapping(RelNode* ast);
shared_ptr<LogicalNode> flattenSubqueries(shared_ptr<LogicalNode> node, const vector<AliasMapping>& aliasMap);
shared_ptr<LogicalNode> mergeProjections(shared_ptr<LogicalNode> node);
shared_ptr<LogicalNode> mergeSelectNodes(shared_ptr<LogicalNode> node);

#endif 
