#ifndef SQL_PARSER_H
#define SQL_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  Operation types  */
typedef enum {
    OP_PROJECT,
    OP_SELECT,
    OP_JOIN,
    OP_RENAME,
    OP_SUBQUERY
} RelOpType;

/*  Condition types  */
typedef enum {
    COND_EQ,
    COND_LT,
    COND_GT,
    COND_LE,
    COND_GE,
    COND_NE,
    COND_AND,
    COND_OR,
    COND_NOT
} CondType;

/*  Column reference  */
typedef struct Column {
    char *table;
    char *attr;
    struct Column *next;
} Column;

/*  Table reference  */
typedef struct Table {
    char *name;
    char *alias;
    struct Table *next;
} Table;

/*  Condition / predicate tree  */
typedef struct Condition {
    CondType type;
    union {
        struct {
            struct Condition *left;
            struct Condition *right;
        } binary;
        struct {
            struct Condition *cond;
        } unary;
        struct {
            char *table;
            char *attr;
            int int_literal;
            float float_literal;
            char *str_literal;
            int literal_type; /* 0: int, 1: float, 2: string, 3: column */
            char *cmp_table;
            char *cmp_attr;
        } comparison;
    } expr;
} Condition;

/*  Relational algebra node (AST)  */
typedef struct RelNode {
    RelOpType op_type;
    union {
        struct {
            struct RelNode *input;
            Column *columns;
        } project;
        struct {
            struct RelNode *input;
            Condition *condition;
        } select;
        struct {
            struct RelNode *left;
            struct RelNode *right;
            Condition *condition;
        } join;
        struct {
            struct RelNode *input;
            char *old_name;
            char *new_name;
        } rename;
        struct {
            struct RelNode *subquery;
            char *alias;
        } subquery;
    } op;
    Table *tables; /* For base relations only */
} RelNode;

/*  Helper function declarations  */
Column *create_column(char *table, char *attr);
Column *append_column(Column *list, Column *new_col);
Table *create_table(char *name, char *alias);
Table *append_table(Table *list, Table *new_table);
Condition *create_comparison(CondType type, char *table, char *attr, int literal_type,
                            int int_val, float float_val, char *str_val,
                            char *cmp_table, char *cmp_attr);
Condition *create_binary_condition(CondType type, Condition *left, Condition *right);
Condition *create_unary_condition(CondType type, Condition *cond);
RelNode *create_project_node(RelNode *input, Column *columns);
RelNode *create_select_node(RelNode *input, Condition *condition);
RelNode *create_join_node(RelNode *left, RelNode *right, Condition *condition);
RelNode *create_rename_node(RelNode *input, char *old_name, char *new_name);
RelNode *create_base_relation(Table *tables);
RelNode *create_subquery_node(RelNode *subquery, char *alias);
void print_ra_tree_json(RelNode *root);
void free_columns(Column *cols);
void free_tables(Table *tables);
void free_condition(Condition *cond);
void free_relnode(RelNode *node);

/*  Dotted name helpers  */
char* get_first_part(const char* dotted_str);
char* get_remaining_part(const char* dotted_str);

/*  Parser globals  */
extern RelNode *result;

#ifdef __cplusplus
}
#endif

#endif /* SQL_PARSER_H */
