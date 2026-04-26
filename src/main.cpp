
// Query Optimizer — Main Entry Point
// Usage:
//   ./query_optimizer                    # Interactive CLI
//   ./query_optimizer test.sql           # Parse & optimize from file
//   ./query_optimizer --server [port]    # Launch web UI server


#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>
#include <sstream>

#include "catalog.h"
#include "logical_plan.h"
#include "physical_plan.h"
#include "heuristic_optimizer.h"
#include "cost_estimator.h"
#include "plan_enumerator.h"
#include "optimizer.h"

using namespace std;

extern "C" {
    #include "sql_parser.h"
    #include "y.tab.h"
    extern FILE *yyin;
    extern int yyparse(void);
    extern RelNode *result;

    typedef struct yy_buffer_state *YY_BUFFER_STATE;
    YY_BUFFER_STATE yy_scan_string(const char *str);
    void yy_delete_buffer(YY_BUFFER_STATE buf);
    void yy_switch_to_buffer(YY_BUFFER_STATE buf);
}

// Defined in http_server.cpp
void startServer(int port, Optimizer& optimizer);

static void printUsage(const char* prog) {
    cerr << "Usage:" << endl;
    cerr << "  " << prog << "                     Interactive mode" << endl;
    cerr << "  " << prog << " <file.sql>           Optimize from file" << endl;
    cerr << "  " << prog << " --batch <file.sql>   Optimize multiple queries from file" << endl;
    cerr << "  " << prog << " --server [port]      Start web UI (default port 8080)" << endl;
}

static void runOptimizer(Optimizer& optimizer) {
    if (result == NULL) {
        cerr << "Error: No parse tree generated." << endl;
        return;
    }

    auto optResult = optimizer.optimize(result);

    cout << optResult.toJSON() << endl;

    // Also print human-readable summary to stderr
    cerr << "\n---------------------------------------------" << endl;
    cerr << "  Query Optimizer Results" << endl;
    cerr << "---------------------------------------------" << endl;
    cerr << "  Unoptimized cost: " << optResult.unoptimizedCost << " ms" << endl;
    cerr << "  Optimized cost:   " << optResult.optimizedCost << " ms" << endl;
    if (optResult.unoptimizedCost > 0) {
        double improvement = (1.0 - optResult.optimizedCost / optResult.unoptimizedCost) * 100;
        cerr << "  Improvement:      " << improvement << "%" << endl;
    }
    cerr << "  Plans evaluated:  " << optResult.allPlans.size() << endl;
    cerr << "---------------------------------------------\n" << endl;
}

int main(int argc, char* argv[]) {
    // Initialize catalog with hardcoded data
    Catalog catalog;
    catalog.initUniversityDB();

    
    
    Optimizer optimizer(catalog);

    //  Server mode 
    if (argc >= 2 && string(argv[1]) == "--server") {
        int port = 8080;
        if (argc >= 3) port = atoi(argv[2]);
        startServer(port, optimizer);
        return 0;
    }

    //  Batch mode (multi-query file) 
    if (argc == 3 && string(argv[1]) == "--batch") {
        ifstream file(argv[2]);
        if (!file.is_open()) {
            cerr << "Error: Cannot open file '" << argv[2] << "'" << endl;
            return 1;
        }
        string line, query;
        int qnum = 0;
        auto runQuery = [&]() {
            if (query.empty()) return;
            qnum++;
            cerr << "\n--- Query #" << qnum << " ---\n" << query << endl;
            result = NULL;
            YY_BUFFER_STATE buf = yy_scan_string(query.c_str());
            yy_switch_to_buffer(buf);
            if (yyparse() == 0) {
                runOptimizer(optimizer);
            } else {
                cerr << "Parse error in query #" << qnum << "." << endl;
            }
            yy_delete_buffer(buf);
            if (result) { free_relnode(result); result = NULL; }
            query.clear();
        };
        while (getline(file, line)) {
            // Strip \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // Semicolons and blank lines both act as query separators
            if (line == ";" || line.find(';') != string::npos) {
                // Remove the semicolon and append any content before it
                size_t pos = line.find(';');
                string before = line.substr(0, pos);
                if (!before.empty()) query += " " + before;
                runQuery();
            } else if (line.find_first_not_of(" \t") == string::npos) {
                // Blank line: separator only if we have content
                if (!query.empty()) runQuery();
            } else {
                query += " " + line;
            }
        }
        // Flush any trailing query
        if (!query.empty()) runQuery();
        cerr << "\n=== Batch done: " << qnum << " queries processed ===" << endl;
        return 0;
    }

    // ── File mode ──
    if (argc == 2) {
        yyin = fopen(argv[1], "r");
        if (!yyin) {
            cerr << "Error: Cannot open file '" << argv[1] << "'" << endl;
            return 1;
        }
        if (yyparse() == 0) {
            runOptimizer(optimizer);
        } else {
            cerr << "Parse error." << endl;
        }
        fclose(yyin);
        free_relnode(result);
        return 0;
    }

    //  Interactive mode 
    if (argc == 1) {
        cerr << "Query Optimizer — Interactive Mode" << endl;
        cerr << "Enter SQL query (Ctrl+D to end):" << endl;
        cerr << "Example: SELECT s.name FROM student s WHERE s.dept_name = 'Comp. Sci.'" << endl;
        cerr << endl;

        yyin = stdin;
        if (yyparse() == 0) {
            runOptimizer(optimizer);
        } else {
            cerr << "Parse error." << endl;
        }
        free_relnode(result);
        return 0;
    }

    printUsage(argv[0]);
    return 1;
}
