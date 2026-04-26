#include "logical_plan.h"
#include <functional>

using namespace std;


// Predicate


set<string> Predicate::getReferencedTables() const {
    set<string> tables;
    if (type == PredType::COMPARISON) {
        if (left.isColumn && !left.table.empty())
            tables.insert(left.table);
        if (right.isColumn && !right.table.empty())
            tables.insert(right.table);
    } else {
        for (const auto& child : children) {
            auto ct = child.getReferencedTables();
            tables.insert(ct.begin(), ct.end());
        }
    }
    return tables;
}

string Predicate::opString() const {
    switch (op) {
        case CompOp::EQ: return "=";
        case CompOp::LT: return "<";
        case CompOp::GT: return ">";
        case CompOp::LE: return "<=";
        case CompOp::GE: return ">=";
        case CompOp::NE: return "<>";
    }
    return "?";
}

string Predicate::toString() const {
    if (type == PredType::COMPARISON) {
        string ls = left.isColumn ? (left.table + "." + left.column) :
                    (left.literalType == 2 ? "'" + left.strVal + "'" : to_string((int)left.numVal));
        string rs = right.isColumn ? (right.table + "." + right.column) :
                    (right.literalType == 2 ? "'" + right.strVal + "'" :
                     (right.literalType == 1 ? to_string(right.numVal) : to_string((int)right.numVal)));
        return ls + " " + opString() + " " + rs;
    } else if (type == PredType::AND) {
        return "(" + children[0].toString() + " AND " + children[1].toString() + ")";
    } else if (type == PredType::OR) {
        return "(" + children[0].toString() + " OR " + children[1].toString() + ")";
    } else if (type == PredType::NOT) {
        return "NOT (" + children[0].toString() + ")";
    }
    return "?";
}

string Predicate::toJSON() const {
    ostringstream ss;
    if (type == PredType::COMPARISON) {
        ss << "{\"type\":\"comparison\",\"op\":\"" << opString() << "\"";
        ss << ",\"left\":{";
        if (left.isColumn) {
            ss << "\"type\":\"column\",\"table\":\"" << left.table << "\",\"column\":\"" << left.column << "\"";
        } else {
            if (left.literalType == 2)
                ss << "\"type\":\"string\",\"value\":\"" << left.strVal << "\"";
            else if (left.literalType == 1)
                ss << "\"type\":\"float\",\"value\":" << left.numVal;
            else
                ss << "\"type\":\"int\",\"value\":" << (int)left.numVal;
        }
        ss << "},\"right\":{";
        if (right.isColumn) {
            ss << "\"type\":\"column\",\"table\":\"" << right.table << "\",\"column\":\"" << right.column << "\"";
        } else {
            if (right.literalType == 2)
                ss << "\"type\":\"string\",\"value\":\"" << right.strVal << "\"";
            else if (right.literalType == 1)
                ss << "\"type\":\"float\",\"value\":" << right.numVal;
            else
                ss << "\"type\":\"int\",\"value\":" << (int)right.numVal;
        }
        ss << "}}";
    } else if (type == PredType::AND || type == PredType::OR) {
        ss << "{\"type\":\"" << (type == PredType::AND ? "AND" : "OR") << "\"";
        ss << ",\"left\":" << children[0].toJSON();
        ss << ",\"right\":" << children[1].toJSON();
        ss << "}";
    } else if (type == PredType::NOT) {
        ss << "{\"type\":\"NOT\",\"child\":" << children[0].toJSON() << "}";
    }
    return ss.str();
}


// ScanNode


ScanNode::ScanNode(const string& table, const string& a) {
    type = LogicalNodeType::SCAN;
    tableName = table;
    alias = a;
}

string ScanNode::toJSON() const {
    ostringstream ss;
    ss << "{\"type\":\"Scan\",\"table\":\"" << tableName << "\"";
    if (!alias.empty()) ss << ",\"alias\":\"" << alias << "\"";
    ss << ",\"estimatedRows\":" << estimatedRows << "}";
    return ss.str();
}

string ScanNode::toString(int indent) const {
    string pad(indent * 2, ' ');
    string s = pad + "Scan(" + tableName;
    if (!alias.empty()) s += " AS " + alias;
    s += ") [rows=" + to_string(estimatedRows) + "]";
    return s;
}

set<string> ScanNode::getOutputTables() const {
    return { effectiveName() };
}

shared_ptr<LogicalNode> ScanNode::clone() const {
    auto n = make_shared<ScanNode>(tableName, alias);
    n->estimatedRows = estimatedRows;
    return n;
}


// SelectNode


SelectNode::SelectNode(shared_ptr<LogicalNode> in, const Predicate& pred) {
    type = LogicalNodeType::SELECT;
    input = in;
    predicate = pred;
}

string SelectNode::toJSON() const {
    ostringstream ss;
    ss << "{\"type\":\"Select\",\"predicate\":" << predicate.toJSON()
       << ",\"estimatedRows\":" << estimatedRows
       << ",\"input\":" << input->toJSON() << "}";
    return ss.str();
}

string SelectNode::toString(int indent) const {
    string pad(indent * 2, ' ');
    return pad + "Select(" + predicate.toString() + ") [rows=" + to_string(estimatedRows) + "]\n"
         + input->toString(indent + 1);
}

set<string> SelectNode::getOutputTables() const {
    return input->getOutputTables();
}

shared_ptr<LogicalNode> SelectNode::clone() const {
    auto n = make_shared<SelectNode>(input->clone(), predicate);
    n->estimatedRows = estimatedRows;
    return n;
}


// ProjectNode


ProjectNode::ProjectNode(shared_ptr<LogicalNode> in, const vector<ColumnRef>& cols) {
    type = LogicalNodeType::PROJECT;
    input = in;
    columns = cols;
}

string ProjectNode::toJSON() const {
    ostringstream ss;
    ss << "{\"type\":\"Project\",\"columns\":[";
    for (size_t i = 0; i < columns.size(); i++) {
        if (i > 0) ss << ",";
        ss << columns[i].toJSON();
    }
    ss << "],\"estimatedRows\":" << estimatedRows
       << ",\"input\":" << input->toJSON() << "}";
    return ss.str();
}

string ProjectNode::toString(int indent) const {
    string pad(indent * 2, ' ');
    string s = pad + "Project(";
    for (size_t i = 0; i < columns.size(); i++) {
        if (i > 0) s += ", ";
        s += columns[i].table + "." + columns[i].column;
    }
    s += ") [rows=" + to_string(estimatedRows) + "]\n" + input->toString(indent + 1);
    return s;
}

set<string> ProjectNode::getOutputTables() const {
    return input->getOutputTables();
}

shared_ptr<LogicalNode> ProjectNode::clone() const {
    auto n = make_shared<ProjectNode>(input->clone(), columns);
    n->estimatedRows = estimatedRows;
    return n;
}


// JoinNode


JoinNode::JoinNode(shared_ptr<LogicalNode> l, shared_ptr<LogicalNode> r, const Predicate& cond) {
    type = LogicalNodeType::JOIN;
    left = l;
    right = r;
    condition = cond;
}

string JoinNode::toJSON() const {
    ostringstream ss;
    ss << "{\"type\":\"Join\",\"condition\":" << condition.toJSON()
       << ",\"estimatedRows\":" << estimatedRows
       << ",\"left\":" << left->toJSON()
       << ",\"right\":" << right->toJSON() << "}";
    return ss.str();
}

string JoinNode::toString(int indent) const {
    string pad(indent * 2, ' ');
    return pad + "Join(" + condition.toString() + ") [rows=" + to_string(estimatedRows) + "]\n"
         + left->toString(indent + 1) + "\n"
         + right->toString(indent + 1);
}

set<string> JoinNode::getOutputTables() const {
    auto l = left->getOutputTables();
    auto r = right->getOutputTables();
    l.insert(r.begin(), r.end());
    return l;
}

shared_ptr<LogicalNode> JoinNode::clone() const {
    auto n = make_shared<JoinNode>(left->clone(), right->clone(), condition);
    n->estimatedRows = estimatedRows;
    return n;
}


// Alias Resolution


vector<AliasMapping> buildAliasMapping(RelNode* ast) {
    vector<AliasMapping> aliasMap;
    
    if (!ast) return aliasMap;
    
    // Helper function to extract base table from a subquery
    function<string(RelNode*)> getBaseTable = [&](RelNode* node) -> string {
        if (!node) return "";
        
        // If this is a base relation (has tables)
        if (node->tables != NULL) {
            Table* t = node->tables;
            if (t && t->name) return t->name;
        }
        
        // Recursively search for base table in child nodes
        switch (node->op_type) {
            case OP_PROJECT:
                return getBaseTable(node->op.project.input);
            case OP_SELECT:
                return getBaseTable(node->op.select.input);
            case OP_JOIN:
                return getBaseTable(node->op.join.left);
            case OP_RENAME:
                return getBaseTable(node->op.rename.input);
            case OP_SUBQUERY:
                return getBaseTable(node->op.subquery.subquery);
            default:
                return "";
        }
    };
    
    // Helper function to extract column mappings from a subquery's projection
    function<void(RelNode*, AliasMapping&)> extractColumnMappings = [&](RelNode* node, AliasMapping& mapping) {
        if (!node) return;
        
        if (node->op_type == OP_PROJECT) {
            Column* cols = node->op.project.columns;
            while (cols) {
                string colName = cols->attr ? cols->attr : "";
                string colTable = cols->table ? cols->table : "";
                if (!colName.empty()) {
                    mapping.columnMapping[colName] = colName;
                }
                cols = cols->next;
            }
        }
        
        // Recursively search for projection in child nodes
        switch (node->op_type) {
            case OP_PROJECT:
                // Already handled above
                break;
            case OP_SELECT:
                extractColumnMappings(node->op.select.input, mapping);
                break;
            case OP_SUBQUERY:
                extractColumnMappings(node->op.subquery.subquery, mapping);
                break;
            default:
                break;
        }
    };
    
    // Main traversal to find subqueries and build mappings
    function<void(RelNode*)> traverse = [&](RelNode* node) {
        if (!node) return;
        
        if (node->op_type == OP_SUBQUERY) {
            string alias = node->op.subquery.alias ? node->op.subquery.alias : "";
            RelNode* subquery = node->op.subquery.subquery;
            
            if (!alias.empty() && subquery) {
                string baseTable = getBaseTable(subquery);
                if (!baseTable.empty()) {
                    AliasMapping mapping;
                    mapping.alias = alias;
                    mapping.baseTable = baseTable;
                    
                    // Extract column mappings from the subquery
                    extractColumnMappings(subquery, mapping);
                    
                    aliasMap.push_back(mapping);
                }
            }
            
            // Recursively process the subquery itself
            traverse(subquery);
        }
        
        // Recursively process child nodes
        switch (node->op_type) {
            case OP_PROJECT:
                traverse(node->op.project.input);
                break;
            case OP_SELECT:
                traverse(node->op.select.input);
                break;
            case OP_JOIN:
                traverse(node->op.join.left);
                traverse(node->op.join.right);
                break;
            case OP_RENAME:
                traverse(node->op.rename.input);
                break;
            default:
                break;
        }
    };
    
    traverse(ast);
    return aliasMap;
}

void resolveColumnRefAliases(ColumnRef& col, const vector<AliasMapping>& aliasMap) {
    for (const auto& mapping : aliasMap) {
        if (col.table == mapping.alias) {
            // Map the column
            auto it = mapping.columnMapping.find(col.column);
            if (it != mapping.columnMapping.end()) {
                col.table = mapping.baseTable;
                col.column = it->second;
            } else {
                // If no specific mapping, keep column name but change table
                col.table = mapping.baseTable;
            }
            break;
        }
    }
}

void resolvePredicateAliases(Predicate& pred, const vector<AliasMapping>& aliasMap) {
    if (pred.type == PredType::COMPARISON) {
        if (pred.left.isColumn) {
            ColumnRef colRef;
            colRef.table = pred.left.table;
            colRef.column = pred.left.column;
            resolveColumnRefAliases(colRef, aliasMap);
            pred.left.table = colRef.table;
            pred.left.column = colRef.column;
        }
        if (pred.right.isColumn) {
            ColumnRef colRef;
            colRef.table = pred.right.table;
            colRef.column = pred.right.column;
            resolveColumnRefAliases(colRef, aliasMap);
            pred.right.table = colRef.table;
            pred.right.column = colRef.column;
        }
    } else {
        // Recursively resolve child predicates
        for (auto& child : pred.children) {
            resolvePredicateAliases(child, aliasMap);
        }
    }
}

shared_ptr<LogicalNode> flattenSubqueries(shared_ptr<LogicalNode> node, const vector<AliasMapping>& aliasMap) {
    if (!node) return nullptr;
    
    if (node->type == LogicalNodeType::PROJECT) {
        auto proj = dynamic_pointer_cast<ProjectNode>(node);
        
        // Resolve column aliases in projection
        for (auto& col : proj->columns) {
            resolveColumnRefAliases(col, aliasMap);
        }
        
        // Recursively flatten input first
        proj->input = flattenSubqueries(proj->input, aliasMap);
        
        // After flattening, check if input is still a Project node
        // If so, we can merge the projections by keeping the outer projection's columns
        // and bypassing the inner projection
        if (proj->input->type == LogicalNodeType::PROJECT) {
            auto innerProj = dynamic_pointer_cast<ProjectNode>(proj->input);
            
            // Check if both projections reference the same base table
            bool sameTable = false;
            if (!proj->columns.empty() && !innerProj->columns.empty()) {
                sameTable = (proj->columns[0].table == innerProj->columns[0].table);
            }
            
            // Merge: keep outer projection columns, skip inner projection
            // The outer projection determines which columns are output
            if (sameTable) {
                proj->input = innerProj->input;
                // Recursively flatten again in case there are more nested projections
                return flattenSubqueries(proj, aliasMap);
            }
        }
        
        return proj;
    }
    
    if (node->type == LogicalNodeType::SELECT) {
        auto sel = dynamic_pointer_cast<SelectNode>(node);
        
        // Resolve predicate aliases
        resolvePredicateAliases(sel->predicate, aliasMap);
        
        // Recursively flatten input
        sel->input = flattenSubqueries(sel->input, aliasMap);
        
        // Check if input is also a Select node on the same table
        // If so, we can merge the predicates with AND
        if (sel->input->type == LogicalNodeType::SELECT) {
            auto innerSel = dynamic_pointer_cast<SelectNode>(sel->input);
            
            // Get referenced tables for both predicates
            auto outerTables = sel->predicate.getReferencedTables();
            auto innerTables = innerSel->predicate.getReferencedTables();
            
            // Check if they reference the same table(s)
            bool sameTable = false;
            if (!outerTables.empty() && !innerTables.empty()) {
                sameTable = (*outerTables.begin() == *innerTables.begin());
            }
            
            if (sameTable) {
                // Merge predicates with AND
                Predicate merged;
                merged.type = PredType::AND;
                merged.children.push_back(innerSel->predicate);
                merged.children.push_back(sel->predicate);
                sel->predicate = merged;
                sel->input = innerSel->input;
                // Recursively flatten again in case there are more nested selects
                return flattenSubqueries(sel, aliasMap);
            }
        }
        
        return sel;
    }
    
    if (node->type == LogicalNodeType::JOIN) {
        auto join = dynamic_pointer_cast<JoinNode>(node);
        resolvePredicateAliases(join->condition, aliasMap);
        join->left = flattenSubqueries(join->left, aliasMap);
        join->right = flattenSubqueries(join->right, aliasMap);
        return join;
    }
    
    if (node->type == LogicalNodeType::SCAN) {
        return node;
    }
    
    return node;
}


// SELECT Node Merging (post-processing pass)


shared_ptr<LogicalNode> mergeSelectNodes(shared_ptr<LogicalNode> node) {
    if (!node) return nullptr;

    // First, recursively process children
    if (node->type == LogicalNodeType::SELECT) {
        auto sel = dynamic_pointer_cast<SelectNode>(node);
        sel->input = mergeSelectNodes(sel->input);
        
        // Check if input is also a Select node
        if (sel->input->type == LogicalNodeType::SELECT) {
            auto innerSel = dynamic_pointer_cast<SelectNode>(sel->input);
            
            // Get referenced tables for both predicates
            auto outerTables = sel->predicate.getReferencedTables();
            auto innerTables = innerSel->predicate.getReferencedTables();
            
            // Check if they reference the same table(s)
            bool sameTable = false;
            if (!outerTables.empty() && !innerTables.empty()) {
                sameTable = (*outerTables.begin() == *innerTables.begin());
            }
            
            if (sameTable) {
                // Merge predicates with AND
                Predicate merged;
                merged.type = PredType::AND;
                merged.children.push_back(innerSel->predicate);
                merged.children.push_back(sel->predicate);
                sel->predicate = merged;
                sel->input = innerSel->input;
                // Recursively merge again in case there are more nested selects
                return mergeSelectNodes(node);
            }
        }
        
        return sel;
    }
    
    if (node->type == LogicalNodeType::PROJECT) {
        auto proj = dynamic_pointer_cast<ProjectNode>(node);
        proj->input = mergeSelectNodes(proj->input);
        return proj;
    }
    
    if (node->type == LogicalNodeType::JOIN) {
        auto join = dynamic_pointer_cast<JoinNode>(node);
        join->left = mergeSelectNodes(join->left);
        join->right = mergeSelectNodes(join->right);
        return join;
    }
    
    if (node->type == LogicalNodeType::SCAN) {
        return node;
    }
    
    return node;
}


// Projection Merging (post-processing pass)


shared_ptr<LogicalNode> mergeProjections(shared_ptr<LogicalNode> node) {
    if (!node) return nullptr;

    // First, recursively process children
    if (node->type == LogicalNodeType::PROJECT) {
        auto proj = dynamic_pointer_cast<ProjectNode>(node);
        proj->input = mergeProjections(proj->input);
        
        // Check if input is a Select node
        if (proj->input->type == LogicalNodeType::SELECT) {
            auto sel = dynamic_pointer_cast<SelectNode>(proj->input);
            
            // Check if the Select's input is a Project on the same table
            if (sel->input->type == LogicalNodeType::PROJECT) {
                auto innerProj = dynamic_pointer_cast<ProjectNode>(sel->input);
                
                // Check if both projections reference the same table
                bool sameTable = false;
                if (!proj->columns.empty() && !innerProj->columns.empty()) {
                    sameTable = (proj->columns[0].table == innerProj->columns[0].table);
                }
                
                // Check if the Select's predicate only references this table
                auto predTables = sel->predicate.getReferencedTables();
                bool onlyThisTable = (predTables.size() == 1 && 
                                      predTables.count(proj->columns[0].table));
                
                if (sameTable && onlyThisTable) {
                    // Merge: outer Project + Select + inner Project -> outer Project + Select
                    // The outer projection determines the output columns
                    sel->input = innerProj->input;
                    // Recursively merge again in case there are more nested structures
                    return mergeProjections(node);
                }
            }
        }
        
        return proj;
    }
    
    if (node->type == LogicalNodeType::SELECT) {
        auto sel = dynamic_pointer_cast<SelectNode>(node);
        sel->input = mergeProjections(sel->input);
        return sel;
    }
    
    if (node->type == LogicalNodeType::JOIN) {
        auto join = dynamic_pointer_cast<JoinNode>(node);
        join->left = mergeProjections(join->left);
        join->right = mergeProjections(join->right);
        return join;
    }
    
    if (node->type == LogicalNodeType::SCAN) {
        return node;
    }
    
    return node;
}


// AST → Logical Plan conversion


static CompOp condTypeToCompOp(CondType ct) {
    switch (ct) {
        case COND_EQ: return CompOp::EQ;
        case COND_LT: return CompOp::LT;
        case COND_GT: return CompOp::GT;
        case COND_LE: return CompOp::LE;
        case COND_GE: return CompOp::GE;
        case COND_NE: return CompOp::NE;
        default: return CompOp::EQ;
    }
}

Predicate convertCondition(Condition* cond) {
    Predicate pred;
    if (!cond) {
        pred.type = PredType::COMPARISON;
        return pred;
    }

    switch (cond->type) {
        case COND_AND:
            pred.type = PredType::AND;
            pred.children.push_back(convertCondition(cond->expr.binary.left));
            pred.children.push_back(convertCondition(cond->expr.binary.right));
            break;
        case COND_OR:
            pred.type = PredType::OR;
            pred.children.push_back(convertCondition(cond->expr.binary.left));
            pred.children.push_back(convertCondition(cond->expr.binary.right));
            break;
        case COND_NOT:
            pred.type = PredType::NOT;
            pred.children.push_back(convertCondition(cond->expr.unary.cond));
            break;
        default: // Comparison
            pred.type = PredType::COMPARISON;
            pred.op = condTypeToCompOp(cond->type);
            pred.left.isColumn = true;
            pred.left.table = cond->expr.comparison.table ? cond->expr.comparison.table : "";
            pred.left.column = cond->expr.comparison.attr ? cond->expr.comparison.attr : "";

            switch (cond->expr.comparison.literal_type) {
                case 0: // int
                    pred.right.isColumn = false;
                    pred.right.numVal = cond->expr.comparison.int_literal;
                    pred.right.literalType = 0;
                    break;
                case 1: // float
                    pred.right.isColumn = false;
                    pred.right.numVal = cond->expr.comparison.float_literal;
                    pred.right.literalType = 1;
                    break;
                case 2: // string
                    pred.right.isColumn = false;
                    pred.right.strVal = cond->expr.comparison.str_literal ? cond->expr.comparison.str_literal : "";
                    pred.right.literalType = 2;
                    break;
                case 3: // column
                    pred.right.isColumn = true;
                    pred.right.table = cond->expr.comparison.cmp_table ? cond->expr.comparison.cmp_table : "";
                    pred.right.column = cond->expr.comparison.cmp_attr ? cond->expr.comparison.cmp_attr : "";
                    break;
            }
            break;
    }
    return pred;
}

shared_ptr<LogicalNode> convertASTtoLogicalPlan(RelNode* ast, const vector<AliasMapping>& parentAliasMap) {
    if (!ast) return nullptr;

    // Build alias mapping from the AST before conversion
    vector<AliasMapping> aliasMap = buildAliasMapping(ast);
    
    // Merge with parent alias map
    for (const auto& parentMapping : parentAliasMap) {
        bool alreadyExists = false;
        for (const auto& localMapping : aliasMap) {
            if (localMapping.alias == parentMapping.alias) {
                alreadyExists = true;
                break;
            }
        }
        if (!alreadyExists) {
            aliasMap.push_back(parentMapping);
        }
    }

    // Base relation: one or more tables (comma-syntax FROM a, b, c...)
    if (ast->tables != NULL) {
        // Collect all tables from the linked list
        vector<pair<string,string>> tableList; // {name, alias}
        for (Table* t = ast->tables; t != NULL; t = t->next) {
            string nm  = t->name  ? t->name  : "";
            string al  = t->alias ? t->alias : "";
            if (!nm.empty()) tableList.push_back({nm, al});
        }

        if (tableList.empty()) return nullptr;

        // Single table — just a ScanNode
        if (tableList.size() == 1) {
            return make_shared<ScanNode>(tableList[0].first, tableList[0].second);
        }

        // Multiple tables — build a left-deep chain of cross-joins.
        // The WHERE-clause equijoin predicates will replace the dummy conditions
        // during heuristic pushdown (pushPredicate folds them in).
        Predicate dummyCond;  // empty table/col = dummy placeholder
        dummyCond.type = PredType::COMPARISON;
        dummyCond.op   = CompOp::EQ;
        // left and right operands stay default (isColumn=false, everything empty)

        shared_ptr<LogicalNode> acc =
            make_shared<ScanNode>(tableList[0].first, tableList[0].second);
        for (size_t i = 1; i < tableList.size(); i++) {
            auto rhs = make_shared<ScanNode>(tableList[i].first, tableList[i].second);
            acc = make_shared<JoinNode>(acc, rhs, dummyCond);
        }
        return acc;
    }


    switch (ast->op_type) {
        case OP_PROJECT: {
            // Check if input is a subquery - if so, inline it
            if (ast->op.project.input && ast->op.project.input->op_type == OP_SUBQUERY) {
                RelNode* subqueryNode = ast->op.project.input->op.subquery.subquery;
                string subAlias = ast->op.project.input->op.subquery.alias ? ast->op.project.input->op.subquery.alias : "";
                
                // Build alias mapping for this subquery
                AliasMapping mapping;
                mapping.alias = subAlias;
                
                // Extract base table from subquery
                function<string(RelNode*)> getBaseTable = [&](RelNode* node) -> string {
                    if (!node) return "";
                    if (node->tables != NULL && node->tables->name) return node->tables->name;
                    switch (node->op_type) {
                        case OP_PROJECT: return getBaseTable(node->op.project.input);
                        case OP_SELECT: return getBaseTable(node->op.select.input);
                        case OP_SUBQUERY: return getBaseTable(node->op.subquery.subquery);
                        default: return "";
                    }
                };
                mapping.baseTable = getBaseTable(subqueryNode);
                
                // Extract column mappings
                function<void(RelNode*, AliasMapping&)> extractCols = [&](RelNode* node, AliasMapping& m) {
                    if (!node) return;
                    if (node->op_type == OP_PROJECT) {
                        Column* cols = node->op.project.columns;
                        while (cols) {
                            if (cols->attr) m.columnMapping[cols->attr] = cols->attr;
                            cols = cols->next;
                        }
                    } else if (node->op_type == OP_SELECT) {
                        extractCols(node->op.select.input, m);
                    } else if (node->op_type == OP_SUBQUERY) {
                        extractCols(node->op.subquery.subquery, m);
                    }
                };
                extractCols(subqueryNode, mapping);
                
                if (!mapping.alias.empty() && !mapping.baseTable.empty()) {
                    aliasMap.push_back(mapping);
                }
                
                // Convert the subquery directly (not as a separate node)
                auto child = convertASTtoLogicalPlan(subqueryNode, aliasMap);
                
                // Build the outer projection with resolved aliases
                vector<ColumnRef> cols;
                Column* c = ast->op.project.columns;
                while (c) {
                    ColumnRef cr;
                    cr.table = c->table ? c->table : "";
                    cr.column = c->attr ? c->attr : "";
                    resolveColumnRefAliases(cr, aliasMap);
                    cols.push_back(cr);
                    c = c->next;
                }
                
                shared_ptr<LogicalNode> proj = make_shared<ProjectNode>(child, cols);
                proj = flattenSubqueries(proj, aliasMap);
                proj = mergeProjections(proj);
                return mergeSelectNodes(proj);
            }
            
            // Normal project
            auto child = convertASTtoLogicalPlan(ast->op.project.input, aliasMap);
            vector<ColumnRef> cols;
            Column* c = ast->op.project.columns;
            while (c) {
                ColumnRef cr;
                cr.table = c->table ? c->table : "";
                cr.column = c->attr ? c->attr : "";
                resolveColumnRefAliases(cr, aliasMap);
                cols.push_back(cr);
                c = c->next;
            }
            shared_ptr<LogicalNode> proj = make_shared<ProjectNode>(child, cols);
            proj = flattenSubqueries(proj, aliasMap);
            proj = mergeProjections(proj);
            return mergeSelectNodes(proj);
        }
        case OP_SELECT: {
            // Check if input is a subquery
            if (ast->op.select.input && ast->op.select.input->op_type == OP_SUBQUERY) {
                RelNode* subqueryNode = ast->op.select.input->op.subquery.subquery;
                string subAlias = ast->op.select.input->op.subquery.alias ? ast->op.select.input->op.subquery.alias : "";
                
                // Build alias mapping for this subquery
                AliasMapping mapping;
                mapping.alias = subAlias;
                
                function<string(RelNode*)> getBaseTable = [&](RelNode* node) -> string {
                    if (!node) return "";
                    if (node->tables != NULL && node->tables->name) return node->tables->name;
                    switch (node->op_type) {
                        case OP_PROJECT: return getBaseTable(node->op.project.input);
                        case OP_SELECT: return getBaseTable(node->op.select.input);
                        case OP_SUBQUERY: return getBaseTable(node->op.subquery.subquery);
                        default: return "";
                    }
                };
                mapping.baseTable = getBaseTable(subqueryNode);
                
                function<void(RelNode*, AliasMapping&)> extractCols = [&](RelNode* node, AliasMapping& m) {
                    if (!node) return;
                    if (node->op_type == OP_PROJECT) {
                        Column* cols = node->op.project.columns;
                        while (cols) {
                            if (cols->attr) m.columnMapping[cols->attr] = cols->attr;
                            cols = cols->next;
                        }
                    } else if (node->op_type == OP_SELECT) {
                        extractCols(node->op.select.input, m);
                    } else if (node->op_type == OP_SUBQUERY) {
                        extractCols(node->op.subquery.subquery, m);
                    }
                };
                extractCols(subqueryNode, mapping);
                
                if (!mapping.alias.empty() && !mapping.baseTable.empty()) {
                    aliasMap.push_back(mapping);
                }
                
                // Convert the subquery directly
                auto child = convertASTtoLogicalPlan(subqueryNode, aliasMap);
                
                // Convert the predicate with resolved aliases
                Predicate pred = convertCondition(ast->op.select.condition);
                resolvePredicateAliases(pred, aliasMap);
                
                shared_ptr<LogicalNode> sel = make_shared<SelectNode>(child, pred);
                sel = flattenSubqueries(sel, aliasMap);
                sel = mergeProjections(sel);
                return mergeSelectNodes(sel);
            }
            
            // Normal select
            auto child = convertASTtoLogicalPlan(ast->op.select.input, aliasMap);
            Predicate pred = convertCondition(ast->op.select.condition);
            resolvePredicateAliases(pred, aliasMap);
            shared_ptr<LogicalNode> sel = make_shared<SelectNode>(child, pred);
            sel = flattenSubqueries(sel, aliasMap);
            sel = mergeProjections(sel);
            return mergeSelectNodes(sel);
        }
        case OP_JOIN: {
            auto left = convertASTtoLogicalPlan(ast->op.join.left, aliasMap);
            auto right = convertASTtoLogicalPlan(ast->op.join.right, aliasMap);
            Predicate cond = convertCondition(ast->op.join.condition);
            resolvePredicateAliases(cond, aliasMap);
            auto join = make_shared<JoinNode>(left, right, cond);
            return flattenSubqueries(join, aliasMap);
        }
        case OP_RENAME: {
            auto child = convertASTtoLogicalPlan(ast->op.rename.input, aliasMap);
            if (auto scan = dynamic_pointer_cast<ScanNode>(child)) {
                scan->alias = ast->op.rename.new_name ? ast->op.rename.new_name : "";
            }
            return child;
        }
        case OP_SUBQUERY: {
            // This should not be reached if subqueries are inlined above
            // But handle it for safety
            auto subPlan = convertASTtoLogicalPlan(ast->op.subquery.subquery, aliasMap);
            return subPlan;
        }
        default:
            return nullptr;
    }
}
